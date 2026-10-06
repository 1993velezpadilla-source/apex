#pragma once
#include <JuceHeader.h>
#include "../../Builds/VisualStudio2026/ArrangementEditor/TimePitchDSPCore.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/VoiceTransformMapperCore.h"

namespace DAW::SoundEngine {

struct FallbackTimePitchContinuityPlan
{
	bool renderedLastBlock = false;
	bool continuousOffset = false;
	bool sameMode = false;
	bool samePath = false;
	bool shouldReset = true;
};

struct FallbackTimePitchContractPlan
{
	bool stretchOnly = false;
	double pitchSemitones = 0.0;
	float fineTuneCents = 0.0f;
	double stretchRatio = 1.0;
	ArrangementEditor::TimePitchMode mode = ArrangementEditor::TimePitchMode::Resample;
};

class ApexFallbackTimePitchContractCore final
{
public:
	static FallbackTimePitchContinuityPlan makeContinuityPlan(bool renderedLastBlock,
															  bool continuousOffset,
															  bool sameMode,
															  bool samePath) noexcept
	{
		FallbackTimePitchContinuityPlan plan;
		plan.renderedLastBlock = renderedLastBlock;
		plan.continuousOffset = continuousOffset;
		plan.sameMode = sameMode;
		plan.samePath = samePath;
		plan.shouldReset = !renderedLastBlock || !continuousOffset || !sameMode || !samePath;
		return plan;
	}

	static bool isIdentityModernMode(float clipPitch,
									 float fineTuneCents,
									 float clipStretch,
									 double formantSemitones,
									 bool preserveFormants) noexcept
	{
		return (std::abs(clipPitch) < 0.0001f)
			&& (std::abs(fineTuneCents) < 0.01f)
			&& (std::abs(clipStretch - 1.0f) < 0.0001f)
			&& (std::abs(formantSemitones) < 0.0001f)
			&& !preserveFormants;
	}

	static FallbackTimePitchContractPlan makeContractPlan(bool stretchOnly,
														  double smoothedClipPitch,
														  float clipStretch,
														  int timePitchModeId) noexcept
	{
		FallbackTimePitchContractPlan plan;
		plan.stretchOnly = stretchOnly;
		plan.pitchSemitones = stretchOnly ? 0.0 : smoothedClipPitch;
		plan.fineTuneCents = 0.0f;
		plan.stretchRatio = clipStretch;
		plan.mode = stretchOnly
			? ArrangementEditor::TimePitchMode::Stretch
			: static_cast<ArrangementEditor::TimePitchMode>(juce::jlimit(0, 6, timePitchModeId));
		return plan;
	}

	static int makeSourceChannel(int outputChannel,
								 int sourceChannels) noexcept
	{
		return juce::jmin(outputChannel, sourceChannels - 1);
	}

	static const char* makeCallerTag(bool stretchOnly,
									 bool normalPath,
									 bool pitchActive,
									 bool stretchActive) noexcept
	{
		return stretchOnly ? "NEW_STRETCH_ONLY"
			: (normalPath && pitchActive && stretchActive) ? "OLD_COMBINED"
			: "DSP_FALLBACK";
	}
};

} // namespace DAW::SoundEngine
