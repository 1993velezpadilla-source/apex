#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../PluginHostCore/PluginChainCore.h"
#include <map>
#include <memory>

namespace DAW::SoundEngine {

struct PluginPdcLatencyPair
{
	int sourceLatencySamples = 0;
	int destinationLatencySamples = 0;
	int delaySamples = 0;
};

class ApexPluginPdcContractCore final
{
public:
	using PluginChainMap = std::map<TrackID, std::unique_ptr<PluginChainCore>>;

	static int getTrackLatencySamples(const PluginChainMap* chains, const TrackID& trackId) noexcept
	{
		if (chains == nullptr || trackId.isEmpty())
			return 0;

		auto it = chains->find(trackId);
		if (it == chains->end() || it->second == nullptr)
			return 0;

		return juce::jmax(0, it->second->totalLatencySamples());
	}

	static int computeSidechainDelaySamples(int sourceLatencySamples, int destinationLatencySamples) noexcept
	{
		return juce::jmax(0, sourceLatencySamples - destinationLatencySamples);
	}

	static int computeDelayLineCapacity(int delaySamples, int numSamples) noexcept
	{
		const int safeSamples = juce::jmax(1, numSamples);
		return juce::jmax(safeSamples * 4, juce::jmax(0, delaySamples) + safeSamples * 2);
	}

	static PluginPdcLatencyPair makeSidechainLatencyPair(const PluginChainMap* chains,
														 const TrackID& sourceTrackId,
														 const TrackID& destinationTrackId) noexcept
	{
		PluginPdcLatencyPair pair;
		pair.sourceLatencySamples = getTrackLatencySamples(chains, sourceTrackId);
		pair.destinationLatencySamples = getTrackLatencySamples(chains, destinationTrackId);
		pair.delaySamples = computeSidechainDelaySamples(pair.sourceLatencySamples, pair.destinationLatencySamples);
		return pair;
	}
};

} // namespace DAW::SoundEngine
