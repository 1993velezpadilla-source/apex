#include <JuceHeader.h>
#include "../../../Source/MidiCore/MidiClip.h"
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
