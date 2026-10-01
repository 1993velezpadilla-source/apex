#include <JuceHeader.h>
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipSplitCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementClipModel.h"
#include "../../../Source/SoundEngineCore/ApexSourceReadContractCore.h"
#include "../../../Source/UtilityCore/Types.h"
#include <cmath>
#include <limits>

using namespace ArrangementEditor;
using namespace DAW::SoundEngine;

class BladeSourceRenderContractTests final : public juce::UnitTest
{
public:
    BladeSourceRenderContractTests() : juce::UnitTest ("blade-source-render-contract.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest ("same-rate source window contiguity at 44100");
        testSourceContiguity (44100.0, 44100.0);

        beginTest ("same-rate source window contiguity at 48000");
        testSourceContiguity (48000.0, 48000.0);

        beginTest ("cross-rate source window contiguity 44100/48000");
        testSourceContiguity (44100.0, 48000.0);

        beginTest ("cross-rate source window contiguity 48000/44100");
        testSourceContiguity (48000.0, 44100.0);

        beginTest ("no NaN or infinity in calculated source positions");
        testNoNaNOrInfinity();

        beginTest ("first right source position equals declared source boundary");
        testRightSourceMatchesBoundary();
    }

private:
    void testSourceContiguity (double srcRate, double devRate)
    {
        juce::ignoreUnused (devRate);
        const int64_t srcTotal = (int64_t) std::round (srcRate * 4.0);

        ArrangementClipModel clip;
        clip.id = juce::Uuid();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.sourceOffset = 0.0;
        clip.sourceStartSample = 0;
        clip.sourceEndSample = srcTotal;
        clip.sourceSampleRate = srcRate;

        const double splitTime = 2.0;
        auto result = ClipSplitCore::splitClip (clip, splitTime);
        expect (result.valid);

        const int64_t boundary = result.left.sourceEndSample;
        expect (boundary > 0);
        expect (boundary < srcTotal);

        const int64_t leftWindowCount = boundary - result.left.sourceStartSample;
        const int64_t rightWindowCount = result.right.sourceEndSample - boundary;
        expectEquals (leftWindowCount + rightWindowCount, srcTotal);

        const int64_t srcOffsetLeft = (int64_t) std::round (clip.sourceOffset * srcRate);
        auto leftPlan = ApexSourceReadContractCore::makeSourceBounds (
            srcOffsetLeft, result.left.sourceStartSample, boundary, srcTotal);
        expect (leftPlan.valid);
        expectEquals (leftPlan.sourceStartBound, (int64_t) 0);
        expectEquals (leftPlan.sourceEndBound, boundary);

        const int64_t srcOffsetRight = (int64_t) std::round (result.right.sourceOffset * srcRate);
        auto rightPlan = ApexSourceReadContractCore::makeSourceBounds (
            srcOffsetRight, boundary, result.right.sourceEndSample, srcTotal);
        expect (rightPlan.valid);
        expectEquals (rightPlan.sourceStartBound, boundary);

        const auto readPlanLeft = ApexSourceReadContractCore::makeReadPlan (
            0, (int) std::round (srcRate * 2.0), 0, (int64_t) std::round (srcRate * 2.0));
        expect (readPlanLeft.intersects);
        expectEquals (readPlanLeft.count, (int) std::round (srcRate * 2.0));
    }

    void testNoNaNOrInfinity()
    {
        const double srcRate = 44100.0;
        ArrangementClipModel clip;
        clip.id = juce::Uuid();
        clip.startTime = 0.5;
        clip.length = 1.5;
        clip.sourceOffset = 0.25;
        clip.sourceStartSample = 11025;
        clip.sourceEndSample = 77175;
        clip.sourceSampleRate = srcRate;

        auto result = ClipSplitCore::splitClip (clip, 1.5);
        expect (result.valid);

        auto checkFinite = [this] (const juce::String& label, double v)
        {
            expect (std::isfinite (v), label + " should be finite, got " + juce::String (v));
        };

        checkFinite ("left.start", result.left.startTime);
        checkFinite ("left.length", result.left.length);
        checkFinite ("left.sourceOffset", result.left.sourceOffset);
        checkFinite ("right.start", result.right.startTime);
        checkFinite ("right.length", result.right.length);
        checkFinite ("right.sourceOffset", result.right.sourceOffset);

        expect (! std::isnan ((double) result.left.sourceStartSample));
        expect (! std::isnan ((double) result.left.sourceEndSample));
        expect (! std::isnan ((double) result.right.sourceStartSample));
        expect (! std::isnan ((double) result.right.sourceEndSample));
    }

    void testRightSourceMatchesBoundary()
    {
        const double srcRate = 48000.0;
        ArrangementClipModel clip;
        clip.id = juce::Uuid();
        clip.startTime = 0.0;
        clip.length = 2.0;
        clip.sourceOffset = 0.0;
        clip.sourceStartSample = 0;
        clip.sourceEndSample = 96000;
        clip.sourceSampleRate = srcRate;

        auto result = ClipSplitCore::splitClip (clip, 1.0);
        expect (result.valid);

        expectEquals (result.right.sourceStartSample, result.left.sourceEndSample);

        const int64_t boundary = result.left.sourceEndSample;
        expectEquals (result.right.sourceStartSample, boundary);
    }
};

static BladeSourceRenderContractTests bladeSourceRenderContractTests;
