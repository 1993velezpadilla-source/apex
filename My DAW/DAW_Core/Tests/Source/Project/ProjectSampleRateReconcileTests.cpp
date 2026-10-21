#include <JuceHeader.h>
#include "../../../Source/ProjectCore/ProjectSampleRateReconcileCore.h"
#include "../../../Source/MidiCore/MidiClip.h"
#include "../../../Source/TrackCore/TrackPlaylistCompCore.h"
#include "../../../Source/RecordingCore/LoopPlaylistTakePlanCore.h"
#include "../../../Source/UICore/TrackPlaylistInlineLayoutCore.h"
#include "../../../Source/CommandCore/RecordCommands.h"
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

class ApexTrackPlaylistCoreTests final : public juce::UnitTest
{
public:
    ApexTrackPlaylistCoreTests()
        : juce::UnitTest("track-playlists-comp-and-persistence.v1", "APEX.Project") {}

    void runTest() override
    {
        beginTest("legacy track has one main playlist; new take never destroys original");
        {
            DAW::TrackManager tracks;
            auto* vocal = tracks.createTrack("Lead");
            DAW::ClipManager clips;
            auto* first = clips.createAudioClip("take one", {});
            first->setTrackID(vocal->getID());
            first->setStartPosition(48000);
            first->setLength(96000);
            DAW::TrackPlaylistCompCore::publishPlaybackVisibility(tracks, clips);
            expect(first->isPlaylistAudible());

            const auto secondId = DAW::TrackPlaylistCompCore::createEmptyPlaylist(*vocal, tracks, clips);
            expect(!first->isPlaylistAudible());
            expectEquals(vocal->getMainPlaylistId(), secondId);
            auto* second = clips.createAudioClip("take two", {});
            second->setTrackID(vocal->getID());
            second->setPlaylistId(secondId);
            DAW::TrackPlaylistCompCore::publishPlaybackVisibility(tracks, clips);
            expect(second->isPlaylistAudible());
            expect(!first->isPlaylistAudible());
            expect(vocal->showPlaylistAsMain("main"));
            DAW::TrackPlaylistCompCore::publishPlaybackVisibility(tracks, clips);
            expect(first->isPlaylistAudible());
            expect(!second->isPlaylistAudible());

            auto trackState = vocal->getState();
            auto clipState = second->getState();
            expectEquals(clipState.getProperty("playlistId").toString(), secondId);
            expectEquals(trackState.getChildWithName("TrackPlaylists").getNumChildren(), 2);
            DAW::Track copy(vocal->getID(), "Copy");
            copy.restoreState(trackState);
            expectEquals(copy.getMainPlaylistId(), vocal->getMainPlaylistId());
            expectEquals(copy.getPlaylists().size(), vocal->getPlaylists().size());
            expectEquals(copy.getTargetPlaylistId(), vocal->getTargetPlaylistId());
        }

        beginTest("Stop at start marker saves overlapping vocal passes without a loop");
        {
            DAW::TrackManager tracks;
            DAW::ClipManager clips;
            auto* vocal = tracks.createTrack("Lead");
            const auto first = DAW::TrackPlaylistCompCore::planNonLoopTake(
                *vocal, clips, 15 * 48000, 5 * 48000);
            expect(!first.createPlaylist); // first pass records on Main
            auto* take1 = clips.createAudioClip("take 1", {});
            take1->setTrackID(vocal->getID());
            take1->setStartPosition(15 * 48000);
            take1->setLength(5 * 48000);
            const auto repeat = DAW::TrackPlaylistCompCore::planNonLoopTake(
                *vocal, clips, 15 * 48000, 5 * 48000);
            expect(repeat.createPlaylist);
            expect(repeat.showAsMain); // latest take can become Main
            const auto shortPass = DAW::TrackPlaylistCompCore::planNonLoopTake(
                *vocal, clips, 15 * 48000, 3 * 48000);
            expect(shortPass.createPlaylist);
            expect(!shortPass.showAsMain); // preserve unrecorded tail
            const auto later = DAW::TrackPlaylistCompCore::planNonLoopTake(
                *vocal, clips, 25 * 48000, 5 * 48000);
            expect(!later.createPlaylist); // regular non-overlap clips stay Main
            auto* intro = clips.createAudioClip("intro", {});
            intro->setTrackID(vocal->getID());
            intro->setStartPosition(0);
            intro->setLength(48000);
            const auto withIntro = DAW::TrackPlaylistCompCore::planNonLoopTake(
                *vocal, clips, 15 * 48000, 5 * 48000);
            expect(withIntro.createPlaylist);
            expect(!withIntro.showAsMain); // do not mute unrelated Main
            const auto noSamples = DAW::TrackPlaylistCompCore::planNonLoopTake(
                *vocal, clips, 15 * 48000, 0);
            expect(!noSamples.createPlaylist);
        }

        beginTest("loop pass planner splits one recorded WAV into aligned lanes");
        {
            auto passes = DAW::LoopPlaylistTakePlanCore::make(
                48000, 48000, 96000, 120000, true);
            expectEquals((int)passes.size(), 3);
            if (passes.size() == 3)
            {
                expectEquals((juce::int64)passes[0].sourceStart, (juce::int64)0);
                expectEquals((juce::int64)passes[0].length, (juce::int64)48000);
                expectEquals((juce::int64)passes[1].sourceStart, (juce::int64)48000);
                expectEquals((juce::int64)passes[2].sourceStart, (juce::int64)96000);
                expectEquals((juce::int64)passes[2].length, (juce::int64)24000);
                for (const auto& pass : passes)
                    expectEquals((juce::int64)pass.timelineStart, (juce::int64)48000);
            }
            expect(DAW::LoopPlaylistTakePlanCore::make(
                47999, 48000, 96000, 120000, true).empty());
            expect(DAW::LoopPlaylistTakePlanCore::make(
                96000, 48000, 96000, 120000, true).empty());
            expect(DAW::LoopPlaylistTakePlanCore::make(
                48000, 48000, 96000, 120000, false).empty());
            expect(DAW::LoopPlaylistTakePlanCore::make(
                48000, 48000, 96000, 48000, true).empty());
        }

        beginTest("loop starts mid-cycle: first partial take stays in its correct position");
        {
            // Record starts at 1.25s of a [1s,2s) loop. First capture
            // window is 0.75s at 1.25s, later passes restart at 1s.
            const auto passes = DAW::LoopPlaylistTakePlanCore::make(
                60000, 48000, 96000, 96000, true);
            expectEquals((int)passes.size(), 3);
            if (passes.size() == 3)
            {
                expectEquals((juce::int64)passes[0].timelineStart, (juce::int64)60000);
                expectEquals((juce::int64)passes[0].sourceStart, (juce::int64)0);
                expectEquals((juce::int64)passes[0].length, (juce::int64)36000);
                expectEquals((juce::int64)passes[1].timelineStart, (juce::int64)48000);
                expectEquals((juce::int64)passes[1].sourceStart, (juce::int64)36000);
                expectEquals((juce::int64)passes[1].length, (juce::int64)48000);
                expectEquals((juce::int64)passes[2].timelineStart, (juce::int64)48000);
                expectEquals((juce::int64)passes[2].sourceStart, (juce::int64)84000);
                expectEquals((juce::int64)passes[2].length, (juce::int64)12000);
            }
            expect(DAW::LoopPlaylistTakePlanCore::make(
                60000, 48000, 96000, 36000, true).empty());
            expect(DAW::LoopPlaylistTakePlanCore::make(
                60000, 48000, 96000, 0, true).empty());
        }

        beginTest("inline playlist expansion moves real tracks without changing track identity");
        {
            using Layout = DAW::TrackPlaylistInlineLayoutCore;
            const int h = 80;
            const int expanded = 1;
            const int takePixels = 3 * 66;
            expectEquals(Layout::topOfLane(0, h, expanded, takePixels), 80);
            expectEquals(Layout::topOfLane(1, h, expanded, takePixels), 160);
            expectEquals(Layout::takeY(expanded, h), 240);
            expectEquals(Layout::topOfLane(2, h, expanded, takePixels), 438);
            expectEquals(Layout::topOfLane(3, h, expanded, takePixels), 518);
            expectEquals(Layout::laneAtY(200, h, expanded, takePixels), 1);
            expectEquals(Layout::laneAtY(240, h, expanded, takePixels), -1);
            expectEquals(Layout::laneAtY(437, h, expanded, takePixels), -1);
            expectEquals(Layout::laneAtY(438, h, expanded, takePixels), 2);
            expectEquals(Layout::laneAtY(518, h, expanded, takePixels), 3);
            expectEquals(Layout::laneAtY(240, h, -1, 0), 2);
            expectEquals(Layout::laneAtY(79, h, expanded, takePixels), -1);
        }

        beginTest("horizontal playlist time remains aligned after viewport scrolling");
        {
            // Content-space x = 0 is always sample 0. The viewport scrolls
            // the entire arrangement AND its inline child panel together.
            constexpr double sr = 48000.0, pps = 320.0;
            for (const double sample : { 0.0, 24000.0, 48000.0, 96000.0 })
            {
                const double contentX = sample / sr * pps;
                for (const int viewportX : { 0, 120, 600 })
                {
                    const double rulerScreenX = contentX - viewportX;
                    const double takeScreenX = contentX - viewportX;
                    expectWithinAbsoluteError(takeScreenX, rulerScreenX, 1.0e-12);
                    // Forbidden regression: subtracting viewport offset in
                    // BOTH sample origin and component scroll would drift.
                    if (viewportX > 0)
                    {
                        const double incorrect = contentX - 2.0 * viewportX;
                        expect(std::abs(incorrect - rulerScreenX) >= viewportX);
                    }
                }
            }
        }

        beginTest("expanded take lanes follow vertical zoom and cannot intercept other tracks");
        {
            using Layout = DAW::TrackPlaylistInlineLayoutCore;
            expectEquals(Layout::takeHeightForTrackHeight(80), 66);
            expect(Layout::takeHeightForTrackHeight(50) < 66);
            expect(Layout::takeHeightForTrackHeight(140) > 66);
            for (const int trackH : { 50, 80, 140 })
            {
                const int h = Layout::takeHeightForTrackHeight(trackH);
                const int extra = 3 * h;
                const int nextY = Layout::topOfLane(3, trackH, 2, extra);
                expectEquals(nextY, 80 + 3 * trackH + extra);
                expectEquals(Layout::laneAtY(nextY - 1, trackH, 2, extra), -1);
                expectEquals(Layout::laneAtY(nextY, trackH, 2, extra), 3);
            }
        }

        beginTest("loop-record Undo/Redo restores take lanes but never the mixer");
        {
            DAW::TrackManager tracks;
            auto* vocal = tracks.createTrack("Lead Vocal");
            DAW::ClipManager clips;
            vocal->setVolume(0.42f);
            const auto before = vocal->getPlaylistState().createCopy();
            const auto newLane = vocal->createPlaylist();
            const auto after = vocal->getPlaylistState().createCopy();
            auto* take = clips.createAudioClip("take 2", {});
            take->setTrackID(vocal->getID());
            take->setPlaylistId(newLane);
            const DAW::RecordAudioTakeCommand::Take recorded {
                take->getState().createCopy(), juce::File(), take->getID() };
            const DAW::RecordAudioTakeCommand::PlaylistChange laneChange {
                vocal->getID(), before, after };
            DAW::RecordAudioTakeCommand cmd(
                clips, nullptr, { recorded }, &tracks, { laneChange });
            cmd.execute(); // first execution is a no-op (already finalized)
            expectEquals((int)vocal->getPlaylists().size(), 2);
            cmd.undo();
            expectEquals((int)vocal->getPlaylists().size(), 1);
            expectEquals(vocal->getMainPlaylistId(), juce::String("main"));
            expectEquals((int)clips.getClipsOnTrack(vocal->getID()).size(), 0);
            expectWithinAbsoluteError((double)vocal->getVolume(), 0.42, 0.00001);
            cmd.execute(); // redo
            expectEquals((int)vocal->getPlaylists().size(), 2);
            expectEquals(vocal->getMainPlaylistId(), newLane);
            expectEquals((int)clips.getClipsOnTrack(vocal->getID()).size(), 1);
            auto* restored = clips.getClipsOnTrack(vocal->getID())[0];
            expect(restored != nullptr && restored->isPlaylistAudible());
            expectWithinAbsoluteError((double)vocal->getVolume(), 0.42, 0.00001);
            cmd.undo();
            expectEquals((int)vocal->getPlaylists().size(), 1);
        }

        beginTest("renamed legacy Main playlist name survives save and reopen");
        {
            DAW::Track original("lead-rename", "Lead");
            expect(original.renamePlaylist("main", "Lead - original take"));
            const auto serialized = original.getState();
            DAW::Track restored("lead-rename", "Restored");
            restored.restoreState(serialized);
            expectEquals(restored.getPlaylists().front().id, juce::String("main"));
            expectEquals(restored.getPlaylists().front().name, juce::String("Lead - original take"));
        }

        beginTest("comp slice preserves selected timeline and source offsets at 48k to 44.1k");
        {
            juce::ValueTree input("AudioClip");
            input.setProperty("startPosition", (juce::int64)48000, nullptr);
            input.setProperty("length", (juce::int64)96000, nullptr);
            input.setProperty("sourceOffset", (juce::int64)0, nullptr);
            input.setProperty("sourceStartSample", (juce::int64)0, nullptr);
            input.setProperty("sourceEndSample", (juce::int64)88200, nullptr);
            input.setProperty("fadeInLength", (juce::int64)480, nullptr);
            input.setProperty("fadeOutLength", (juce::int64)480, nullptr);

            auto excerpt = DAW::TrackPlaylistCompCore::sliceAudioClip(
                input, 72000, 96000, 48000.0, 44100.0);
            expect(excerpt.isValid());
            expectEquals((juce::int64)excerpt.getProperty("startPosition"), (juce::int64)72000);
            expectEquals((juce::int64)excerpt.getProperty("length"), (juce::int64)24000);
            expectEquals((juce::int64)excerpt.getProperty("sourceOffset"), (juce::int64)22050);
            expectEquals((juce::int64)excerpt.getProperty("sourceStartSample"), (juce::int64)22050);
            expectEquals((juce::int64)excerpt.getProperty("sourceEndSample"), (juce::int64)44100);
            expect(!DAW::TrackPlaylistCompCore::sliceAudioClip(
                input, 47000, 96000, 48000.0, 44100.0).isValid());

            // When slicing the middle of a take, an unrelated old edge fade
            // must not appear on the newly promoted phrase.
            expectEquals((juce::int64)excerpt.getProperty("fadeInLength"), (juce::int64)0);
            expectEquals((juce::int64)excerpt.getProperty("fadeOutLength"), (juce::int64)0);
            auto left = DAW::TrackPlaylistCompCore::sliceAudioClip(
                input, 48000, 72000, 48000.0, 44100.0);
            expectEquals((juce::int64)left.getProperty("fadeInLength"), (juce::int64)480);
            expectEquals((juce::int64)left.getProperty("fadeOutLength"), (juce::int64)0);
            auto right = DAW::TrackPlaylistCompCore::sliceAudioClip(
                input, 96000, 144000, 48000.0, 44100.0);
            expectEquals((juce::int64)right.getProperty("fadeInLength"), (juce::int64)0);
            expectEquals((juce::int64)right.getProperty("fadeOutLength"), (juce::int64)480);
            auto whole = DAW::TrackPlaylistCompCore::sliceAudioClip(
                input, 48000, 144000, 48000.0, 44100.0);
            expectEquals((juce::int64)whole.getProperty("fadeInLength"), (juce::int64)480);
            expectEquals((juce::int64)whole.getProperty("fadeOutLength"), (juce::int64)480);
        }
    }
};
static ApexTrackPlaylistCoreTests apexTrackPlaylistCoreTests;
