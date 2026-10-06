#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW::SoundEngine {

struct PitchTimeInputPlan
{
	double stretchRatio = 1.0;
	int inputSamplesNeeded = 1;
	int safeInputSamples = 1;
	int64_t inputClipOffset = 0;
};

struct PitchTimeSourcePositionPlan
{
	double srcPosBase = 0.0;
	double srcStep = 1.0;
};

class ApexPitchTimeInputPlanCore final
{
public:
	static PitchTimeInputPlan makeInputPlan(int outputSampleCount,
											SamplePosition engineClipOffset,
											float clipStretch,
											int inputBufferSamples) noexcept
	{
		PitchTimeInputPlan plan;
		plan.stretchRatio = juce::jmax(0.01, (double)clipStretch);
		plan.inputSamplesNeeded = juce::jmax(1, (int)std::llround((double)outputSampleCount / plan.stretchRatio));
		plan.safeInputSamples = juce::jmin(plan.inputSamplesNeeded, inputBufferSamples);
		plan.inputClipOffset = (int64_t)std::llround((double)engineClipOffset / plan.stretchRatio);
		return plan;
	}

	static PitchTimeSourcePositionPlan makeSourcePositionPlan(bool clipReversed,
															 int64_t sourceStartBound,
															 int64_t sourceEndBound,
															 int64_t inputClipOffset) noexcept
	{
		PitchTimeSourcePositionPlan plan;
		plan.srcPosBase = clipReversed
			? (double)(sourceEndBound - 1) - (double)inputClipOffset
			: (double)sourceStartBound + (double)inputClipOffset;
		plan.srcStep = clipReversed ? -1.0 : 1.0;
		return plan;
	}
};

} // namespace DAW::SoundEngine
