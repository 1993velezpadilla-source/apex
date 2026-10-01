#include <JuceHeader.h>
#include "../../../Builds/VisualStudio2026/ArrangementEditor/BladeSplitPlanCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementClipModel.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementClipStateCore.h"
#include "../../../Source/AudioEngineCore/AudioFileManager.h"
#include <cmath>

using namespace ArrangementEditor;

class BladeSplitPlanCoreTests final : public juce::UnitTest
{
public:
    BladeSplitPlanCoreTests() : juce::UnitTest ("blade-split-plan.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest ("makePlan produces valid plan for midpoint split");
        {
            ArrangementClipModel clip;
            clip.id = juce::Uuid();
            clip.startTime = 0.0;
            clip.length = 2.0;
            clip.sourceOffset = 0.0;
            clip.sourceStartSample = 0;
            clip.sourceEndSample = 88200;
            clip.sourceSampleRate = 44100.0;
            clip.fadeInLength = 0.01f;
            clip.fadeOutLength = 0.02f;

            juce::ValueTree engineState("AudioClip");
            engineState.setProperty("startPosition", (juce::int64)0, nullptr);
            engineState.setProperty("length", (juce::int64)88200, nullptr);
            engineState.setProperty("sourceOffset", (juce::int64)0, nullptr);
            engineState.setProperty("sourceStartSample", (juce::int64)0, nullptr);
            engineState.setProperty("sourceEndSample", (juce::int64)88200, nullptr);
            engineState.setProperty("fadeInLength", (juce::int64)441, nullptr);
            engineState.setProperty("fadeOutLength", (juce::int64)882, nullptr);

            BladeSplitPlanInput input;
            input.visual = clip;
            input.engineState = engineState;
            input.engineStart = 0;
            input.engineLength = 88200;
            input.processedTimelineLength = 88200;
            input.sourceOffset = 0;
            input.sourceTotalSamples = 88200;
            input.sourceSampleRate = 44100.0;
            input.deviceSampleRate = 44100.0;
            input.splitTime = 1.0;

            auto plan = BladeSplitPlanCore::makePlan(input);
            expect (plan.valid, "plan should be valid for midpoint split");

            expect (plan.leftEngineState.isValid());
            expect (plan.rightEngineState.isValid());

            const auto leftLen = (int64_t) plan.leftEngineState.getProperty("length", 0);
            const auto rightLen = (int64_t) plan.rightEngineState.getProperty("length", 0);
            expectEquals (leftLen + rightLen, (juce::int64) 88200);

            const auto rightStart = (int64_t) plan.rightEngineState.getProperty("startPosition", 0);
            expect (rightStart > 0);

            const auto leftSrcEnd = (int64_t) plan.leftEngineState.getProperty("sourceEndSample", 0);
            const auto rightSrcStart = (int64_t) plan.rightEngineState.getProperty("sourceStartSample", 0);
            expectEquals (leftSrcEnd, rightSrcStart);
            expect (plan.sourceBoundary == leftSrcEnd);

            expectWithinAbsoluteError ((int64_t) plan.leftEngineState.getProperty("fadeOutLength", (juce::int64)0), (int64_t) 0, (int64_t) 1);
            expectWithinAbsoluteError ((int64_t) plan.rightEngineState.getProperty("fadeInLength", (juce::int64)0), (int64_t) 0, (int64_t) 1);
        }

        beginTest ("makePlan rejects split at clip boundaries");
        {
            ArrangementClipModel clip;
            clip.id = juce::Uuid();
            clip.startTime = 1.0;
            clip.length = 2.0;

            BladeSplitPlanInput input;
            input.visual = clip;
            input.engineStart = 44100;
            input.engineLength = 88200;
            input.processedTimelineLength = 88200;
            input.sourceSampleRate = 44100.0;
            input.deviceSampleRate = 44100.0;
            input.splitTime = 0.5;
            auto planBefore = BladeSplitPlanCore::makePlan(input);
            expect (! planBefore.valid, "split before clip start should be rejected");

            input.splitTime = 4.0;
            auto planAfter = BladeSplitPlanCore::makePlan(input);
            expect (! planAfter.valid, "split after clip end should be rejected");
        }

        beginTest ("makePlan with cross-rate 44100 source on 48000 device");
        {
            ArrangementClipModel clip;
            clip.id = juce::Uuid();
            clip.startTime = 0.0;
            clip.length = 2.0;

            const double srcRate = 44100.0;
            const double devRate = 48000.0;
            const int64_t srcSamples = (int64_t) std::round (srcRate * 2.0);

            juce::ValueTree engineState("AudioClip");
            engineState.setProperty("startPosition", (juce::int64)0, nullptr);
            engineState.setProperty("length", (juce::int64)srcSamples, nullptr);
            engineState.setProperty("sourceOffset", (juce::int64)0, nullptr);
            engineState.setProperty("sourceStartSample", (juce::int64)0, nullptr);
            engineState.setProperty("sourceEndSample", (juce::int64)srcSamples, nullptr);

            BladeSplitPlanInput input;
            input.visual = clip;
            input.engineState = engineState;
            input.engineStart = 0;
            input.engineLength = srcSamples;
            input.processedTimelineLength = srcSamples;
            input.sourceOffset = 0;
            input.sourceTotalSamples = srcSamples;
            input.sourceSampleRate = srcRate;
            input.deviceSampleRate = devRate;
            input.splitTime = 1.0;

            auto plan = BladeSplitPlanCore::makePlan(input);
            expect (plan.valid);

            const auto leftSrcEnd = (int64_t) plan.leftEngineState.getProperty("sourceEndSample", 0);
            const auto rightSrcStart = (int64_t) plan.rightEngineState.getProperty("sourceStartSample", 0);
            expectEquals (leftSrcEnd, rightSrcStart);
            expect (leftSrcEnd > 0);
            expect (leftSrcEnd < srcSamples);
        }
    }
};

static BladeSplitPlanCoreTests bladeSplitPlanCoreTests;
