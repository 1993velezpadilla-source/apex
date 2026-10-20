#include <JuceHeader.h>
#include "../../../Source/MidiCore/MidiClip.h"
#include "../../../Source/ProjectCore/ProjectResetMixCore.h"
#include <cmath>

/**
    MIDI/default clip timing at professional sample rates (2026-07-25).

    Before this fix, MidiClip hardcoded its default length to 352800
    samples (4 bars @120 BPM at 44.1 kHz only) and had no device-rate-change
    update path. Now the default derives from the actual engine rate
    (8 seconds at any rate) and setSampleRate preserves the clip's time
    (and tick) extent across device rate changes.
*/
class ClipTimingDefaultTests final : public juce::UnitTest
{
public:
    ClipTimingDefaultTests() : juce::UnitTest ("clip.timing-defaults.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest ("default MIDI clip is 8 seconds (4 bars @120 BPM) at every rate");
        {
            const double rates[] = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            for (const double rate : rates)
            {
                DAW::MidiClip clip (DAW::ClipID ("t"), "t", rate);
                const double seconds = (double) clip.getLength() / rate;
                expect (std::abs (seconds - 8.0) <= 0.001,
                        juce::String ("default length seconds at ") + juce::String (rate)
                            + ": " + juce::String (seconds, 6));
            }
        }

        beginTest ("default construction without a rate keeps the legacy 44.1k length");
        {
            DAW::MidiClip clip (DAW::ClipID ("t"), "t");
            expectEquals ((juce::int64) clip.getLength(), (juce::int64) 352800);
        }

        beginTest ("setSampleRate preserves the clip's length in seconds");
        {
            DAW::MidiClip clip (DAW::ClipID ("t"), "t", 44100.0);
            clip.setLength ((DAW::SamplePosition) 66150); // 1.5 s at 44.1k (a trimmed clip)
            clip.setSampleRate (96000.0);
            const double seconds = (double) clip.getLength() / 96000.0;
            expectWithinAbsoluteError (seconds, 1.5, 0.001);
        }

        beginTest ("setSampleRate keeps tick position of the clip end stable");
        {
            DAW::MidiClip clip (DAW::ClipID ("t"), "t", 48000.0);
            const auto ticksBefore = clip.samplesToTicks (clip.getLength());
            clip.setSampleRate (88200.0);
            const auto ticksAfter = clip.samplesToTicks (clip.getLength());
            expect (std::abs ((double) (ticksAfter - ticksBefore)) <= 2.0,
                    juce::String ("tick extent drifted: ") + juce::String ((juce::int64) ticksBefore)
                        + " -> " + juce::String ((juce::int64) ticksAfter));
        }

        beginTest ("same-rate setSampleRate is a no-op for length");
        {
            DAW::MidiClip clip (DAW::ClipID ("t"), "t", 48000.0);
            const auto length = clip.getLength();
            clip.setSampleRate (48000.0);
            expectEquals ((juce::int64) clip.getLength(), (juce::int64) length);
        }

        beginTest ("zero/negative rate is ignored safely");
        {
            DAW::MidiClip clip (DAW::ClipID ("t"), "t", 48000.0);
            const auto length = clip.getLength();
            clip.setSampleRate (0.0);
            expectWithinAbsoluteError (clip.getSampleRate(), 48000.0, 0.001);
            expectEquals ((juce::int64) clip.getLength(), (juce::int64) length);
            clip.setSampleRate (-96000.0);
            expectWithinAbsoluteError (clip.getSampleRate(), 48000.0, 0.001);
            expectEquals ((juce::int64) clip.getLength(), (juce::int64) length);
        }

        beginTest ("ticksToSamples / samplesToTicks round-trip at 96 kHz");
        {
            DAW::MidiClip clip (DAW::ClipID ("t"), "t", 96000.0);
            // 120 BPM, 960 PPQ: samplesPerTick = 96000*60/(120*960) = 50.
            expectEquals ((juce::int64) clip.ticksToSamples (960), (juce::int64) 48000);
            expectEquals ((juce::int64) clip.samplesToTicks (48000), (juce::int64) 960);
        }
    }
};

static ClipTimingDefaultTests clipTimingDefaultTests;

// Reset Mix guard — built into the existing arrangement unit-test target.
// The transform must not mutate its input snapshot or relocate audio.
class ResetProjectMixStateTests final : public juce::UnitTest
{
public:
    ResetProjectMixStateTests() : juce::UnitTest("project.reset-mix.keep-recordings.v1", "APEX.Project") {}

    void runTest() override
    {
        beginTest("reset strips mix/effects/automation but preserves audio and master routing");
        juce::ValueTree original("DAWProject");
        juce::ValueTree tracks("Tracks");
        juce::ValueTree track("Track");
        track.setProperty("id", "TRK_001", nullptr);
        track.setProperty("volume", 0.35f, nullptr);
        track.setProperty("pan", -0.6f, nullptr);
        track.setProperty("muted", true, nullptr);
        track.setProperty("hasPlugins", true, nullptr);
        tracks.addChild(track, -1, nullptr);
        original.addChild(tracks, -1, nullptr);
        juce::ValueTree clips("Clips");
        juce::ValueTree clip("AudioClip");
        clip.setProperty("id", "clip.1", nullptr);
        clip.setProperty("trackID", "TRK_001", nullptr);
        clip.setProperty("sourceFile", "C:/records/take1.wav", nullptr);
        clip.setProperty("startPosition", (juce::int64)48000, nullptr);
        clip.setProperty("length", (juce::int64)96000, nullptr);
        clip.setProperty("sourceOffset", (juce::int64)14400, nullptr);
        clip.setProperty("gain", 0.3f, nullptr);
        clip.setProperty("pitch", 7.0f, nullptr);
        clip.setProperty("timeStretch", 1.7f, nullptr);
        clip.setProperty("fadeInLength", (juce::int64)4000, nullptr);
        clips.addChild(clip, -1, nullptr);
        original.addChild(clips, -1, nullptr);
        auto chains = juce::ValueTree("PluginChains");
        chains.addChild(juce::ValueTree("TrackPlugins"), -1, nullptr);
        original.addChild(chains, -1, nullptr);
        auto automation = juce::ValueTree("Automation");
        automation.addChild(juce::ValueTree("Lane"), -1, nullptr);
        original.addChild(automation, -1, nullptr);
        original.addChild(juce::ValueTree("APEXAutomation"), -1, nullptr);
        auto routing = juce::ValueTree("RoutingGraph");
        auto connections = juce::ValueTree("Connections");
        auto route = [](int t) {
            juce::ValueTree c("Connection");
            c.setProperty("type", t, nullptr);
            return c;
        };
        connections.addChild(route((int)DAW::ConnectionType::Direct), -1, nullptr);
        connections.addChild(route((int)DAW::ConnectionType::Send), -1, nullptr);
        connections.addChild(route((int)DAW::ConnectionType::Sidechain), -1, nullptr);
        connections.addChild(route((int)DAW::ConnectionType::FolderSum), -1, nullptr);
        routing.addChild(connections, -1, nullptr);
        original.addChild(routing, -1, nullptr);

        juce::ValueTree cleaned;
        DAW::ProjectResetMixCore::Counts counts;
        juce::String error;
        expect(DAW::ProjectResetMixCore::prepare(original, cleaned, counts, error), error);
        expect(cleaned.isValid());
        auto ct = cleaned.getChildWithName("Tracks").getChild(0);
        expectWithinAbsoluteError((double)ct.getProperty("volume"), 1.0, 0.00001);
        expectWithinAbsoluteError((double)ct.getProperty("pan"), 0.0, 0.00001);
        expect(!(bool)ct.getProperty("muted"));
        expect(!(bool)ct.getProperty("hasPlugins"));
        auto cc = cleaned.getChildWithName("Clips").getChild(0);
        expectEquals(cc.getProperty("sourceFile").toString(), juce::String("C:/records/take1.wav"));
        expectEquals((juce::int64)cc.getProperty("startPosition"), (juce::int64)48000);
        expectEquals((juce::int64)cc.getProperty("length"), (juce::int64)96000);
        expectEquals((juce::int64)cc.getProperty("sourceOffset"), (juce::int64)14400);
        expectWithinAbsoluteError((double)cc.getProperty("gain"), 1.0, 0.00001);
        expectWithinAbsoluteError((double)cc.getProperty("pitch"), 0.0, 0.00001);
        expectWithinAbsoluteError((double)cc.getProperty("timeStretch"), 1.0, 0.00001);
        expectEquals((juce::int64)cc.getProperty("fadeInLength"), (juce::int64)0);
        expectEquals(cleaned.getChildWithName("PluginChains").getNumChildren(), 0);
        expectEquals(cleaned.getChildWithName("Automation").getNumChildren(), 0);
        expect(!cleaned.getChildWithName("APEXAutomation").isValid());
        auto retained = cleaned.getChildWithName("RoutingGraph").getChildWithName("Connections");
        expectEquals(retained.getNumChildren(), 2);
        expectEquals((int)retained.getChild(0).getProperty("type"), (int)DAW::ConnectionType::Direct);
        expectEquals((int)retained.getChild(1).getProperty("type"), (int)DAW::ConnectionType::FolderSum);
        // Original snapshot, including all FX and automation, stays lossless.
        expectEquals(original.getChildWithName("PluginChains").getNumChildren(), 1);
        expectEquals(original.getChildWithName("Automation").getNumChildren(), 1);
        expectEquals(original.getChildWithName("RoutingGraph").getChildWithName("Connections").getNumChildren(), 4);
        expectWithinAbsoluteError((double)original.getChildWithName("Tracks").getChild(0).getProperty("volume"), 0.35, 0.001);

        beginTest("invalid snapshot fails closed without generating a partial reset");
        juce::ValueTree invalid("DAWProject"), rejected;
        DAW::ProjectResetMixCore::Counts rejectedCounts;
        juce::String why;
        expect(!DAW::ProjectResetMixCore::prepare(invalid, rejected, rejectedCounts, why));
        expect(!rejected.isValid());
        expect(why.isNotEmpty());
    }
};
static ResetProjectMixStateTests resetProjectMixStateTests;
