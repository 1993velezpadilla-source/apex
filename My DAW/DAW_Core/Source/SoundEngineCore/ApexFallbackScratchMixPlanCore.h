#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW::SoundEngine {

struct FallbackScratchMixSamplePlan
{
	int posInClip = 0;
	float preFadeSample = 0.0f;
};

class ApexFallbackScratchMixPlanCore final
{
public:
	static FallbackScratchMixSamplePlan makeSamplePlan(float scratchSample,
													   float clipGain,
													   float panGain,
													   SamplePosition engineClipOffset,
													   int sampleIndex) noexcept
	{
		FallbackScratchMixSamplePlan plan;
		plan.posInClip = (int)(engineClipOffset + sampleIndex);
		plan.preFadeSample = scratchSample * clipGain * panGain;
		return plan;
	}
};

} // namespace DAW::SoundEngine
