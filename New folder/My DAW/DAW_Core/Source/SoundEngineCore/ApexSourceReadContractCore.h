#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW::SoundEngine {

struct SourceReadPlan
{
	bool intersects = false;
	int bufferStart = 0;
	int bufferEnd = 0;
	int count = 0;
	SamplePosition engineClipOffset = 0;
};

struct SourceBoundsPlan
{
	bool valid = false;
	int64_t sourceStartBound = 0;
	int64_t sourceEndBound = 0;
};

class ApexSourceReadContractCore final
{
public:
	static SourceReadPlan makeReadPlan(SamplePosition playPosition,
									   int numSamples,
									   SamplePosition clipStart,
									   SamplePosition clipTimelineLength) noexcept
	{
		SourceReadPlan plan;
		if (numSamples <= 0 || clipTimelineLength <= 0)
			return plan;

		const auto clipEnd = clipStart + clipTimelineLength;
		const auto windowEnd = playPosition + numSamples;
		if (playPosition >= clipEnd || windowEnd <= clipStart)
			return plan;

		plan.bufferStart = (int) juce::jmax((SamplePosition) 0, clipStart - playPosition);
		plan.bufferEnd = (int) juce::jmin((SamplePosition) numSamples, clipEnd - playPosition);
		plan.count = plan.bufferEnd - plan.bufferStart;
		if (plan.count <= 0)
			return {};

		plan.engineClipOffset = (playPosition + plan.bufferStart) - clipStart;
		plan.intersects = true;
		return plan;
	}

	static SourceBoundsPlan makeSourceBounds(int64_t clipSourceOffset,
											 int64_t sourceStartSample,
											 int64_t sourceEndSample,
											 int64_t sourceTotalSamples) noexcept
	{
		SourceBoundsPlan plan;
		const int64_t safeTotal = juce::jmax<int64_t>(0, sourceTotalSamples);
		plan.sourceStartBound = juce::jmax<int64_t>(clipSourceOffset, sourceStartSample);
		plan.sourceEndBound = sourceEndSample > 0
			? juce::jmin<int64_t>(sourceEndSample, safeTotal)
			: safeTotal;
		plan.valid = plan.sourceEndBound > plan.sourceStartBound;
		return plan;
	}
};

} // namespace DAW::SoundEngine
