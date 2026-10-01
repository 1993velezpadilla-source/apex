// ===========================================================================
// BladeSplitPlanCore.h
// Pure function that computes engine-state property changes for a blade split.
// No manager mutation, logging, or file I/O.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include "TimePitchSerializationCore.h"
#include "../../../Source/UtilityCore/Types.h"
#include <JuceHeader.h>
#include <cmath>

namespace ArrangementEditor
{

struct BladeSplitPlanInput
{
    ArrangementClipModel visual;
    juce::ValueTree engineState;
    DAW::SamplePosition engineStart = 0;
    DAW::SamplePosition engineLength = 0;
    DAW::SamplePosition processedTimelineLength = 0;
    DAW::SamplePosition sourceOffset = 0;
    int64_t sourceTotalSamples = 0;
    double sourceSampleRate = 0.0;
    double deviceSampleRate = 0.0;
    double splitTime = 0.0;
};

struct BladeSplitPlan
{
    bool valid = false;
    ArrangementClipModel leftVisual;
    ArrangementClipModel rightVisual;
    juce::ValueTree leftEngineState;
    juce::ValueTree rightEngineState;
    DAW::SamplePosition sourceBoundary = 0;
};

class BladeSplitPlanCore final
{
public:
    static BladeSplitPlan makePlan (const BladeSplitPlanInput& input)
    {
        BladeSplitPlan plan;

        if (! input.engineState.isValid())
            return plan;

        const double totalTimelineSec = juce::jmax (0.000001, input.visual.visualLength());
        const double leftTimelineSec  = juce::jmax (0.0, input.splitTime - input.visual.startTime);
        const double splitFraction    = juce::jlimit (0.0, 1.0, leftTimelineSec / totalTimelineSec);

        if (splitFraction <= 0.0 || splitFraction >= 1.0)
            return plan;

        const auto leftEngineLen  = (DAW::SamplePosition) std::round ((double) input.engineLength * splitFraction);
        const auto rightEngineLen = input.engineLength - leftEngineLen;
        const auto leftTimelineLen = (DAW::SamplePosition) std::round ((double) input.processedTimelineLength * splitFraction);
        const auto splitSample    = input.engineStart + leftTimelineLen;

        const double rateRatio = (input.sourceSampleRate > 0.0 && input.deviceSampleRate > 0.0)
            ? input.sourceSampleRate / input.deviceSampleRate : 1.0;
        const auto rightSrcOffset = input.sourceOffset
            + (DAW::SamplePosition) std::round ((double) leftEngineLen * rateRatio);

        plan.leftEngineState  = input.engineState.createCopy();
        plan.rightEngineState = input.engineState.createCopy();

        plan.leftEngineState .setProperty ("length", (juce::int64) leftEngineLen, nullptr);
        plan.rightEngineState.setProperty ("startPosition", (juce::int64) splitSample, nullptr);
        plan.rightEngineState.setProperty ("length", (juce::int64) rightEngineLen, nullptr);
        plan.rightEngineState.setProperty ("sourceOffset", (juce::int64) rightSrcOffset, nullptr);

        const int64_t origSrcStart = input.engineState.getProperty ("sourceStartSample", (int64_t) 0);
        const int64_t origSrcEnd   = input.engineState.getProperty ("sourceEndSample", (int64_t) 0);
        const bool hasExplicitBounds = origSrcEnd > origSrcStart;

        if (hasExplicitBounds)
        {
            const double srcLen = (double) (origSrcEnd - origSrcStart);
            const int64_t leftSrcEnd = origSrcStart + (int64_t) std::round (srcLen * splitFraction);

            plan.leftEngineState .setProperty ("sourceStartSample", (juce::int64) origSrcStart, nullptr);
            plan.leftEngineState .setProperty ("sourceEndSample", (juce::int64) leftSrcEnd, nullptr);
            plan.rightEngineState.setProperty ("sourceStartSample", (juce::int64) leftSrcEnd, nullptr);
            plan.rightEngineState.setProperty ("sourceEndSample", (juce::int64) origSrcEnd, nullptr);
            plan.sourceBoundary = leftSrcEnd;
        }
        else
        {
            const int64_t fallbackBoundary = juce::jmax ((int64_t) 1, rightSrcOffset);
            plan.sourceBoundary = fallbackBoundary;
        }

        if (plan.leftEngineState .hasProperty ("fadeOutLength"))
            plan.leftEngineState .setProperty ("fadeOutLength", (juce::int64) 0, nullptr);
        if (plan.rightEngineState.hasProperty ("fadeInLength"))
            plan.rightEngineState.setProperty ("fadeInLength", (juce::int64) 0, nullptr);

        plan.leftVisual  = input.visual;
        plan.rightVisual = input.visual;
        plan.leftVisual.id            = juce::Uuid();
        plan.leftVisual.length        = input.visual.length * splitFraction;
        plan.leftVisual.fadeOutLength = 0.f;
        plan.rightVisual.id           = juce::Uuid();
        plan.rightVisual.startTime    = input.splitTime;
        plan.rightVisual.length       = input.visual.length * (1.0 - splitFraction);
        plan.rightVisual.sourceOffset = input.visual.sourceOffset + plan.leftVisual.length;
        plan.rightVisual.fadeInLength = 0.f;

        if (hasExplicitBounds)
        {
            const int64_t srcLen = origSrcEnd - origSrcStart;
            const int64_t leftSrcEnd = origSrcStart + (int64_t) std::round ((double) srcLen * splitFraction);
            plan.leftVisual.sourceStartSample  = origSrcStart;
            plan.leftVisual.sourceEndSample    = leftSrcEnd;
            plan.rightVisual.sourceStartSample = leftSrcEnd;
            plan.rightVisual.sourceEndSample   = origSrcEnd;
        }

        plan.leftVisual.timePitch  = ArrangementEditor::TimePitchSerializationCore::inheritForSplit (input.visual.timePitch);
        plan.rightVisual.timePitch = ArrangementEditor::TimePitchSerializationCore::inheritForSplit (input.visual.timePitch);

        plan.valid = true;
        return plan;
    }
};

} // namespace ArrangementEditor
