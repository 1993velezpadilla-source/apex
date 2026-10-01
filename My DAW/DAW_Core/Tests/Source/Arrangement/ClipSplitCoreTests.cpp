#include <JuceHeader.h>
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipSplitCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementClipModel.h"
#include <cmath>

using namespace ArrangementEditor;

class ClipSplitCoreTests final : public juce::UnitTest
{
public:
    ClipSplitCoreTests() : juce::UnitTest ("clip-split-algebra.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest ("same-rate 44100/44100 split at midpoint");
        testSplitAlgebra (44100.0, 44100.0);

        beginTest ("same-rate 48000/48000 split at midpoint");
        testSplitAlgebra (48000.0, 48000.0);

        beginTest ("cross-rate 44100/48000 split at midpoint");
        testSplitAlgebra (44100.0, 48000.0);

        beginTest ("cross-rate 48000/44100 split at midpoint");
        testSplitAlgebra (48000.0, 44100.0);

        beginTest ("nonzero source start and offset");
        testNonzeroSourceStartAndOffset();

        beginTest ("split inside block-sized interval");
        testBlockSizedInterval();

        beginTest ("left/right timeline contiguity");
        testTimelineContiguity();

        beginTest ("left/right source contiguity");
        testSourceContiguity();

        beginTest ("outer fades preserved cut-edge fades zero");
        testFadePreservation();

        beginTest ("timePitch copied exactly");
        testTimePitchInheritance();

        beginTest ("invalid start/end split rejected");
        testInvalidSplitsRejected();

        beginTest ("three repeated splits preserve original union");
        testRepeatedSplitsUnion();
    }

private:
    static ArrangementClipModel makeClip (double startTime, double length,
                                          double sourceOffset, int64_t sourceStart,
                                          int64_t sourceEnd,
                                          float fadeIn, float fadeOut,
                                          double sampleRate = 44100.0)
    {
        ArrangementClipModel c;
        c.id = juce::Uuid();
        c.startTime = startTime;
        c.length = length;
        c.sourceOffset = sourceOffset;
        c.sourceStartSample = sourceStart;
        c.sourceEndSample = sourceEnd;
        c.sourceSampleRate = sampleRate;
        c.fadeInLength = fadeIn;
        c.fadeOutLength = fadeOut;
        return c;
    }

    void testSplitAlgebra (double sourceRate, double deviceRate)
    {
        juce::ignoreUnused (deviceRate);
        const int64_t totalSamples = (int64_t) std::round (sourceRate * 4.0);
        auto clip = makeClip (1.0, 2.0, 0.5, 22050, 22050 + (int64_t) std::round (sourceRate * 2.0),
                              0.01f, 0.02f, sourceRate);

        const double splitTime = 2.0;
        auto result = ClipSplitCore::splitClip (clip, splitTime);
        expect (result.valid, "split should be valid");

        expect (result.left.startTime == clip.startTime);
        expectWithinAbsoluteError (result.left.length, 1.0, 1e-9);
        expectWithinAbsoluteError (result.right.length, 1.0, 1e-9);
        expect (result.right.startTime == splitTime);

        const double splitFraction = 0.5;
        const int64_t srcLen = clip.sourceEndSample - clip.sourceStartSample;
        const int64_t expectedLeftEnd = clip.sourceStartSample + (int64_t) std::round (srcLen * splitFraction);
        expectEquals (result.left.sourceEndSample, expectedLeftEnd);
        expectEquals (result.right.sourceStartSample, expectedLeftEnd);
        expectEquals (result.right.sourceEndSample, clip.sourceEndSample);
        expectEquals (result.left.sourceStartSample, clip.sourceStartSample);

        expectWithinAbsoluteError (result.right.sourceOffset,
            clip.sourceOffset + result.left.length, 1e-9);
    }

    void testNonzeroSourceStartAndOffset()
    {
        const double srcRate = 48000.0;
        const int64_t srcStart = 12000;
        const int64_t srcEnd = 12000 + (int64_t) std::round (srcRate * 3.0);
        auto clip = makeClip (0.0, 3.0, 1.5, srcStart, srcEnd, 0.0f, 0.0f, srcRate);

        const double splitTime = 1.5;
        auto result = ClipSplitCore::splitClip (clip, splitTime);
        expect (result.valid);

        expectEquals (result.left.sourceStartSample, srcStart);
        const int64_t srcLen = srcEnd - srcStart;
        const double frac = 0.5;
        const int64_t expectedBoundary = srcStart + (int64_t) std::round (srcLen * frac);
        expectEquals (result.left.sourceEndSample, expectedBoundary);
        expectEquals (result.right.sourceStartSample, expectedBoundary);
        expectEquals (result.right.sourceEndSample, srcEnd);

        expectWithinAbsoluteError (result.right.sourceOffset, 1.5 + 1.5, 1e-9);
    }

    void testBlockSizedInterval()
    {
        const int blockSize = 512;
        const double sr = 44100.0;
        const double blockSec = (double) blockSize / sr;
        const double clipStart = 0.5;
        const double clipLen = blockSec * 4.0;
        const int64_t srcEnd = (int64_t) std::round (sr * clipLen);
        auto clip = makeClip (clipStart, clipLen, 0.0, 0, srcEnd, 0.0f, 0.0f, sr);

        const double splitTime = clipStart + blockSec * 1.5;
        auto result = ClipSplitCore::splitClip (clip, splitTime);
        expect (result.valid);

        expect (result.left.sourceStartSample == 0);
        expect (result.left.sourceEndSample > 0);
        expect (result.right.sourceStartSample == result.left.sourceEndSample);
        expect (result.right.sourceEndSample == srcEnd);
        expect (result.right.sourceEndSample > result.right.sourceStartSample);
    }

    void testTimelineContiguity()
    {
        auto clip = makeClip (0.0, 2.0, 0.0, 0, 88200, 0.0f, 0.0f, 44100.0);
        const double splitTime = 1.25;

        auto result = ClipSplitCore::splitClip (clip, splitTime);
        expect (result.valid);

        expectWithinAbsoluteError (result.left.endTime(), splitTime, 1e-9);
        expect (result.right.startTime == splitTime);

        const double gap = result.right.startTime - result.left.endTime();
        expectWithinAbsoluteError (gap, 0.0, 1e-9);
    }

    void testSourceContiguity()
    {
        auto clip = makeClip (0.0, 2.0, 0.0, 0, 88200, 0.0f, 0.0f, 44100.0);
        auto result = ClipSplitCore::splitClip (clip, 1.0);
        expect (result.valid);

        expectEquals (result.right.sourceStartSample, result.left.sourceEndSample);
        expect (result.left.sourceEndSample > result.left.sourceStartSample);
        expect (result.right.sourceEndSample > result.right.sourceStartSample);
    }

    void testFadePreservation()
    {
        auto clip = makeClip (0.0, 2.0, 0.0, 0, 88200, 0.05f, 0.1f, 44100.0);
        auto result = ClipSplitCore::splitClip (clip, 1.0);
        expect (result.valid);

        expectWithinAbsoluteError (result.left.fadeInLength, 0.05f, 1e-6f);
        expectWithinAbsoluteError (result.left.fadeOutLength, 0.0f, 1e-6f);
        expectWithinAbsoluteError (result.right.fadeInLength, 0.0f, 1e-6f);
        expectWithinAbsoluteError (result.right.fadeOutLength, 0.1f, 1e-6f);
    }

    void testTimePitchInheritance()
    {
        auto clip = makeClip (0.0, 2.0, 0.0, 0, 88200, 0.0f, 0.0f, 44100.0);
        clip.timePitch.pitchSemitones = 3.0;
        clip.timePitch.stretchRatio = 1.5;
        clip.timePitch.formantSemitones = -2.0;
        clip.timePitch.mode = TimePitchMode::Vocal;
        clip.timePitch.preserveFormants = true;

        auto result = ClipSplitCore::splitClip (clip, 1.0);
        expect (result.valid);

        expectWithinAbsoluteError (result.left.timePitch.pitchSemitones, 3.0, 1e-9);
        expectWithinAbsoluteError (result.left.timePitch.stretchRatio, 1.5, 1e-9);
        expect (result.left.timePitch.mode == TimePitchMode::Vocal);
        expect (result.left.timePitch.preserveFormants == true);

        expectWithinAbsoluteError (result.right.timePitch.pitchSemitones, 3.0, 1e-9);
        expectWithinAbsoluteError (result.right.timePitch.stretchRatio, 1.5, 1e-9);
        expect (result.right.timePitch.mode == TimePitchMode::Vocal);
        expect (result.right.timePitch.preserveFormants == true);
    }

    void testInvalidSplitsRejected()
    {
        auto clip = makeClip (1.0, 2.0, 0.0, 0, 88200, 0.0f, 0.0f, 44100.0);

        auto before = ClipSplitCore::splitClip (clip, 0.5);
        expect (! before.valid, "split before clip start should be rejected");

        auto after = ClipSplitCore::splitClip (clip, 4.0);
        expect (! after.valid, "split after clip end should be rejected");

        auto atStart = ClipSplitCore::splitClip (clip, 1.0);
        expect (! atStart.valid, "split at exact start should be rejected");

        auto atEnd = ClipSplitCore::splitClip (clip, 3.0);
        expect (! atEnd.valid, "split at exact end should be rejected");
    }

    void testRepeatedSplitsUnion()
    {
        auto clip = makeClip (0.0, 2.0, 0.0, 0, 88200, 0.0f, 0.0f, 44100.0);

        auto r1 = ClipSplitCore::splitClip (clip, 0.75);
        expect (r1.valid);

        auto r2 = ClipSplitCore::splitClip (r1.left, 0.375);
        expect (r2.valid);

        auto r3 = ClipSplitCore::splitClip (r1.right, 1.5);
        expect (r3.valid);

        const int64_t unionStart = r2.left.sourceStartSample;
        const int64_t unionEnd = r3.right.sourceEndSample;
        expect (unionEnd > unionStart);

        const double totalLength = r2.left.length + r2.right.length + r3.left.length + r3.right.length;
        expectWithinAbsoluteError (totalLength, clip.length, 1e-6);

        const double totalVisualStart = r2.left.startTime;
        const double totalVisualEnd = r3.right.endTime();
        expectWithinAbsoluteError (totalVisualEnd - totalVisualStart, clip.length, 1e-6);
    }
};

static ClipSplitCoreTests clipSplitCoreTests;
