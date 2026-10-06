#pragma once
#include <JuceHeader.h>
#include "../RoutingCore/RoutingSnapshot.h"
#include <unordered_map>

namespace DAW::SoundEngine {

class ApexRoutingBufferCore final
{
public:
	void prepare(int blockSize, int expectedNodeCount)
	{
		blockSize_ = juce::jmax(1, blockSize);
		reserve(expectedNodeCount);
	}

	void releaseResources()
	{
		nodeBuffers_.clear();
		sidechainBuffers_.clear();
		blockSize_ = 512;
	}

	void syncFromSnapshot(const RoutingSnapshot& snapshot, int numSamples)
	{
		const int capacity = juce::jmax(juce::jmax(1, numSamples), blockSize_);
		reserve((int) snapshot.nodes.size() + 4);

		for (const auto& node : snapshot.nodes)
		{
			if (node.id.isEmpty())
				continue;

			auto& nodeBuffer = nodeBuffers_[node.id];
			ensureStereoCapacity(nodeBuffer, capacity);

			auto& sidechainBuffer = sidechainBuffers_[node.id];
			ensureStereoCapacity(sidechainBuffer, capacity);
		}
	}

	void clearSnapshotAudio(const RoutingSnapshot& snapshot, int numSamples) noexcept
	{
		for (const auto& nodeId : snapshot.processingOrder)
		{
			if (auto* buffer = findNodeBuffer(nodeId))
				buffer->clear(0, numSamples);

			if (auto* buffer = findSidechainBuffer(nodeId))
				buffer->clear(0, numSamples);
		}
	}

	void clearAllAudio() noexcept
	{
		for (auto& entry : nodeBuffers_)
			entry.second.clear();

		for (auto& entry : sidechainBuffers_)
			entry.second.clear();
	}

	juce::AudioBuffer<float>* findNodeBuffer(const juce::String& nodeId) noexcept
	{
		auto it = nodeBuffers_.find(nodeId);
		return it != nodeBuffers_.end() ? &it->second : nullptr;
	}

	juce::AudioBuffer<float>* findSidechainBuffer(const juce::String& nodeId) noexcept
	{
		auto it = sidechainBuffers_.find(nodeId);
		return it != sidechainBuffers_.end() ? &it->second : nullptr;
	}

private:
	struct JuceStringHash
	{
		size_t operator()(const juce::String& value) const noexcept { return (size_t) value.hashCode64(); }
	};

	static void ensureStereoCapacity(juce::AudioBuffer<float>& buffer, int capacity)
	{
		if (buffer.getNumChannels() < 2 || buffer.getNumSamples() < capacity)
			buffer.setSize(2, capacity, false, false, true);
	}

	void reserve(int expectedNodeCount)
	{
		const auto capacity = (size_t) juce::jmax(8, expectedNodeCount);
		nodeBuffers_.reserve(capacity);
		sidechainBuffers_.reserve(capacity);
	}

	int blockSize_ = 512;
	std::unordered_map<juce::String, juce::AudioBuffer<float>, JuceStringHash> nodeBuffers_;
	std::unordered_map<juce::String, juce::AudioBuffer<float>, JuceStringHash> sidechainBuffers_;
};

} // namespace DAW::SoundEngine
