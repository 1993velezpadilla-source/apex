#pragma once
#include <JuceHeader.h>

namespace DAW::SoundEngine {

class ApexMixFanoutCore final
{
public:
	static void addStereo(const float* srcL, const float* srcR,
						  float* dstL, float* dstR,
						  int numSamples) noexcept
	{
		if (!isValid(srcL, srcR, dstL, dstR, numSamples))
			return;

		for (int s = 0; s < numSamples; ++s)
		{
			dstL[s] += srcL[s];
			dstR[s] += srcR[s];
		}
	}

	static void addStereoWithGain(const float* srcL, const float* srcR,
								  const float* gainRamp,
								  float* dstL, float* dstR,
								  int numSamples) noexcept
	{
		if (!isValid(srcL, srcR, dstL, dstR, numSamples) || gainRamp == nullptr)
			return;

		for (int s = 0; s < numSamples; ++s)
		{
			dstL[s] += srcL[s] * gainRamp[s];
			dstR[s] += srcR[s] * gainRamp[s];
		}
	}

	static void addStereoWithGainAndMute(const float* srcL, const float* srcR,
										 const float* gainRamp,
										 const float* muteRamp,
										 float* dstL, float* dstR,
										 int numSamples) noexcept
	{
		if (!isValid(srcL, srcR, dstL, dstR, numSamples) || gainRamp == nullptr || muteRamp == nullptr)
			return;

		for (int s = 0; s < numSamples; ++s)
		{
			const float g = gainRamp[s] * muteRamp[s];
			dstL[s] += srcL[s] * g;
			dstR[s] += srcR[s] * g;
		}
	}

	static void applyStereoMuteRamp(float* dstL, float* dstR,
									const float* muteRamp,
									int numSamples) noexcept
	{
		if (dstL == nullptr || dstR == nullptr || muteRamp == nullptr || numSamples <= 0)
			return;

		for (int s = 0; s < numSamples; ++s)
		{
			dstL[s] *= muteRamp[s];
			dstR[s] *= muteRamp[s];
		}
	}

	static void copyStereo(const float* srcL, const float* srcR,
						   float* dstL, float* dstR,
						   int numSamples) noexcept
	{
		if (!isValid(srcL, srcR, dstL, dstR, numSamples))
			return;

		juce::FloatVectorOperations::copy(dstL, srcL, numSamples);
		juce::FloatVectorOperations::copy(dstR, srcR, numSamples);
	}

	static void clearStereo(float* dstL, float* dstR, int numSamples) noexcept
	{
		if (dstL == nullptr || dstR == nullptr || numSamples <= 0)
			return;

		juce::FloatVectorOperations::clear(dstL, numSamples);
		juce::FloatVectorOperations::clear(dstR, numSamples);
	}

private:
	static bool isValid(const float* srcL, const float* srcR,
						const float* dstL, const float* dstR,
						int numSamples) noexcept
	{
		return srcL != nullptr && srcR != nullptr && dstL != nullptr && dstR != nullptr && numSamples > 0;
	}
};

} // namespace DAW::SoundEngine
