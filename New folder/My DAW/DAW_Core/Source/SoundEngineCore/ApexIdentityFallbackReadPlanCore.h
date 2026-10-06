#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW::SoundEngine {

struct IdentityFallbackReadPlan
{
	int64_t sourceDelta = 0;
	int64_t sourceIndex = 0;
	int posInClip = 0;
	bool valid = false;
};

class ApexIdentityFallbackReadPlanCore final
{
public:
	static IdentityFallbackReadPlan makeReadPlan(SamplePosition engineClipOffset,
												 int sampleIndex,
												 double sourcePerEngineSample,
												 bool clipReversed,
												 int64_t sourceStartBound,
												 int64_t sourceEndBound,
												 int64_t sourceTotalSamples) noexcept
	{
		IdentityFallbackReadPlan plan;
		plan.sourceDelta = (int64_t)std::llround(((double)engineClipOffset + sampleIndex) * sourcePerEngineSample);
		plan.sourceIndex = clipReversed
			? (sourceEndBound - 1 - plan.sourceDelta)
			: (sourceStartBound + plan.sourceDelta);
		plan.posInClip = (int)(engineClipOffset + sampleIndex);
		plan.valid = plan.sourceIndex >= sourceStartBound
			&& plan.sourceIndex < sourceEndBound
			&& plan.sourceIndex < sourceTotalSamples;
		return plan;
	}
};

} // namespace DAW::SoundEngine
