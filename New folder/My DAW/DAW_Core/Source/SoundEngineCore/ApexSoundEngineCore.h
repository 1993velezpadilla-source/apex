#pragma once
#include <JuceHeader.h>
#include "../RoutingCore/RoutingSnapshot.h"
#include <atomic>

namespace DAW::SoundEngine {

enum class RenderMode
{
	Live,
	Offline
};

struct RenderContext
{
	double sampleRate = 44100.0;
	int blockSize = 512;
	int numSamples = 0;
	SamplePosition timelineSample = 0;
	bool isPlaying = false;
	RenderMode mode = RenderMode::Live;

	bool isOffline() const noexcept { return mode == RenderMode::Offline; }
	bool isValid() const noexcept { return sampleRate > 0.0 && blockSize > 0 && numSamples > 0; }
};

struct RenderDiagnostics
{
	std::atomic<int64_t> invalidContexts { 0 };
	std::atomic<int64_t> invalidSnapshots { 0 };
	std::atomic<int64_t> missingMasterBlocks { 0 };
	std::atomic<int64_t> sanitizedSamples { 0 };
};

class ApexSoundEngineCore final
{
public:
	void prepare(double sampleRate, int blockSize) noexcept
	{
		sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
		blockSize_ = juce::jmax(1, blockSize);
	}

	void releaseResources() noexcept
	{
		sampleRate_ = 44100.0;
		blockSize_ = 512;
	}

	RenderContext makeContext(int numSamples,
							  SamplePosition timelineSample,
							  bool isPlaying,
							  RenderMode mode) const noexcept
	{
		RenderContext context;
		context.sampleRate = sampleRate_;
		context.blockSize = blockSize_;
		context.numSamples = numSamples;
		context.timelineSample = timelineSample;
		context.isPlaying = isPlaying;
		context.mode = mode;
		return context;
	}

	bool validateContext(const RenderContext& context) noexcept
	{
		if (context.isValid())
			return true;

		diagnostics_.invalidContexts.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	bool validateSnapshot(const RoutingSnapshot* snapshot) noexcept
	{
		if (!isSnapshotUsable(snapshot))
		{
			diagnostics_.invalidSnapshots.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		return true;
	}

	bool finalizeMasterContract(bool masterWasProcessed,
								const RenderContext& context,
								const juce::AudioSourceChannelInfo& output) noexcept
	{
		if (masterWasProcessed)
			return true;

		diagnostics_.missingMasterBlocks.fetch_add(1, std::memory_order_relaxed);

		if (context.mode == RenderMode::Live)
			output.clearActiveBufferRegion();
		else
			jassertfalse;

		return false;
	}

	int sanitizeOutput(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
	{
		const int safeSamples = juce::jlimit(0, buffer.getNumSamples() - juce::jlimit(0, buffer.getNumSamples(), startSample), numSamples);
		const int safeStart = juce::jlimit(0, buffer.getNumSamples(), startSample);
		const int channels = juce::jmin(2, buffer.getNumChannels());
		int sanitized = 0;

		for (int ch = 0; ch < channels; ++ch)
		{
			auto* data = buffer.getWritePointer(ch, safeStart);
			for (int s = 0; s < safeSamples; ++s)
			{
				const float v = data[s];
				if (!std::isfinite(v))
				{
					data[s] = 0.0f;
					++sanitized;
				}
			}
		}

		if (sanitized > 0)
			diagnostics_.sanitizedSamples.fetch_add(sanitized, std::memory_order_relaxed);

		return sanitized;
	}

	const RenderDiagnostics& getDiagnostics() const noexcept { return diagnostics_; }

	static bool isSnapshotUsable(const RoutingSnapshot* snapshot) noexcept
	{
		if (snapshot == nullptr || snapshot->nodes.empty() || snapshot->processingOrder.empty())
			return false;

		bool masterInOrder = false;
		for (const auto& nodeId : snapshot->processingOrder)
		{
			if (nodeId.isEmpty())
				return false;

			bool foundNode = false;
			for (const auto& node : snapshot->nodes)
			{
				if (node.id == nodeId)
				{
					foundNode = true;
					if (node.type == RoutingNodeType::Master)
						masterInOrder = true;
					break;
				}
			}

			if (!foundNode)
				return false;
		}

		return masterInOrder;
	}

private:
	double sampleRate_ = 44100.0;
	int blockSize_ = 512;
	RenderDiagnostics diagnostics_;
};

} // namespace DAW::SoundEngine
