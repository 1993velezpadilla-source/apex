#pragma once
#include <JuceHeader.h>
#include "../../Builds/VisualStudio2026/ArrangementEditor/TimePitchDSPCore.h"
#include "ApexFallbackTimePitchContractCore.h"

namespace DAW::SoundEngine {

class ApexFallbackTimePitchStateCore final
{
public:
	static ArrangementEditor::TimePitchState makeState(const FallbackTimePitchContractPlan& contractPlan,
													   bool stretchOnly,
													   bool preserveFormants,
													   double formantSemitones) noexcept
	{
		ArrangementEditor::TimePitchState state;
		state.pitchSemitones = contractPlan.pitchSemitones;
		state.fineTuneCents = contractPlan.fineTuneCents;
		if (stretchOnly)
			state.voiceTransform = ArrangementEditor::VoiceTransformState{};
		state.stretchRatio = contractPlan.stretchRatio;
		state.mode = contractPlan.mode;
		state.preserveFormants = preserveFormants;
		state.formantSemitones = formantSemitones;
		return state;
	}
};

} // namespace DAW::SoundEngine
