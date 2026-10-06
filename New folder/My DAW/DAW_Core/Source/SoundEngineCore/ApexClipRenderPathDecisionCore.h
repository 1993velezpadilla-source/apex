#pragma once
#include <JuceHeader.h>

namespace DAW::SoundEngine {

enum class ApexClipRenderPath
{
	Normal,
	StretchOnly,
	PitchOnly,
	StretchThenPitch
};

struct ClipRenderPathDecisionPlan
{
	bool pitchActive = false;
	bool stretchActive = false;
	bool independentPitchMode = false;
	bool independentStretchMode = false;
	ApexClipRenderPath renderPath = ApexClipRenderPath::Normal;
	bool useIndependentPitchPath = false;
};

class ApexClipRenderPathDecisionCore final
{
public:
	static ClipRenderPathDecisionPlan makeDecision(float clipPitch,
												   float smoothedClipPitch,
												   float fineTuneCents,
												   float clipStretch,
												   int timePitchModeId,
												   int pitchOnlyModeId,
												   int vocalModeId,
												   int stretchModeId,
												   bool clipReversed) noexcept
	{
		ClipRenderPathDecisionPlan plan;
		plan.pitchActive = std::abs(clipPitch) > 0.001f
			|| std::abs(smoothedClipPitch) > 0.001f
			|| std::abs(fineTuneCents) > 0.1f;
		plan.stretchActive = std::abs(clipStretch - 1.0f) > 0.001f;
		plan.independentPitchMode = timePitchModeId == pitchOnlyModeId
			|| timePitchModeId == vocalModeId;
		plan.independentStretchMode = timePitchModeId == stretchModeId;

		if (plan.pitchActive && plan.stretchActive && plan.independentPitchMode)
			plan.renderPath = ApexClipRenderPath::StretchThenPitch;
		else if (plan.stretchActive && !plan.pitchActive)
			plan.renderPath = ApexClipRenderPath::StretchOnly;
		else if (plan.pitchActive && plan.independentPitchMode)
			plan.renderPath = ApexClipRenderPath::PitchOnly;
		else
			plan.renderPath = ApexClipRenderPath::Normal;

		plan.useIndependentPitchPath = clipReversed
			|| plan.renderPath == ApexClipRenderPath::PitchOnly
			|| plan.renderPath == ApexClipRenderPath::StretchOnly
			|| plan.renderPath == ApexClipRenderPath::StretchThenPitch;
		return plan;
	}
};

} // namespace DAW::SoundEngine
