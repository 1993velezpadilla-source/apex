#pragma once
#include <JuceHeader.h>
#include "../../Builds/VisualStudio2026/ArrangementEditor/TimePitchDSPCore.h"
#include "ApexFallbackTimePitchContractCore.h"

namespace DAW::SoundEngine {

class ApexFallbackTimePitchRequestCore final
{
public:
	static ArrangementEditor::TimePitchRenderRequest makeRequest(const float* sourceData,
																 int sourceTotalSamples,
																 int64_t sourceStartSample,
																 int64_t sourceEndSample,
																 float* outputData,
																 int numOutputSamples,
																 int channel,
																 double outputSampleRate,
																 const char* callerTag) noexcept
	{
		ArrangementEditor::TimePitchRenderRequest req;
		req.sourceData = sourceData;
		req.sourceTotalSamples = sourceTotalSamples;
		req.sourceStartSample = sourceStartSample;
		req.sourceEndSample = sourceEndSample;
		req.outputData = outputData;
		req.numOutputSamples = numOutputSamples;
		req.channel = channel;
		req.outputSampleRate = outputSampleRate;
		req.callerTag = callerTag;
		return req;
	}

	static void applyStateContract(ArrangementEditor::TimePitchRenderRequest& req,
								   const FallbackTimePitchContractPlan& contractPlan,
								   bool preserveFormants,
								   double formantSemitones) noexcept
	{
		req.state.pitchSemitones = contractPlan.pitchSemitones;
		req.state.fineTuneCents = contractPlan.fineTuneCents;
		req.state.voiceTransform = ArrangementEditor::VoiceTransformState{};
		req.state.stretchRatio = contractPlan.stretchRatio;
		req.state.mode = contractPlan.mode;
		req.state.preserveFormants = preserveFormants;
		req.state.formantSemitones = formantSemitones;
	}
};

} // namespace DAW::SoundEngine
