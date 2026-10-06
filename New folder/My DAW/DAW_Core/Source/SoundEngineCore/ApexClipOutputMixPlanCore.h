#pragma once
#include <JuceHeader.h>

namespace DAW::SoundEngine {

struct ClipOutputMixChannelPlan
{
	int sourceChannel = 0;
};

struct ClipTapeStopChannelPlan
{
	int rightChannel = 0;
};

class ApexClipOutputMixPlanCore final
{
public:
	static ClipOutputMixChannelPlan makeChannelPlan(int outputChannel,
													int sourceChannels) noexcept
	{
		ClipOutputMixChannelPlan plan;
		plan.sourceChannel = juce::jmin(outputChannel, juce::jmax(0, sourceChannels - 1));
		return plan;
	}

	static float getPanGainForChannel(int outputChannel,
									  int renderChannels,
									  float leftPanGain,
									  float rightPanGain) noexcept
	{
		if (renderChannels <= 1)
			return 1.0f;

		return outputChannel == 0 ? leftPanGain : rightPanGain;
	}

	static ClipTapeStopChannelPlan makeTapeStopChannelPlan(int renderChannels) noexcept
	{
		ClipTapeStopChannelPlan plan;
		plan.rightChannel = juce::jmin(1, juce::jmax(0, renderChannels - 1));
		return plan;
	}
};

} // namespace DAW::SoundEngine
