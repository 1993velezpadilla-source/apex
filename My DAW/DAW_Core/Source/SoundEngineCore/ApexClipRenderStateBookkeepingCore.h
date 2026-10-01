#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW::SoundEngine {

struct ClipRenderStateUpdatePlan
{
	SamplePosition endOffset = 0;
	int64_t processBlock = 0;
	int mode = 0;
	int path = 0;
};

class ApexClipRenderStateBookkeepingCore final
{
public:
	static ClipRenderStateUpdatePlan makeCompletedRenderPlan(SamplePosition engineClipOffset,
															 int sampleCount,
															 int64_t processCounter,
															 int timePitchMode,
															 int renderPath) noexcept
	{
		ClipRenderStateUpdatePlan plan;
		plan.endOffset = engineClipOffset + sampleCount;
		plan.processBlock = processCounter;
		plan.mode = timePitchMode;
		plan.path = renderPath;
		return plan;
	}
};

} // namespace DAW::SoundEngine
