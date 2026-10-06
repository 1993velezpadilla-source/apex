#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include <atomic>

namespace DAW::SoundEngine {

struct ClipRenderContext
{
	SamplePosition timelineSample = 0;
	int numSamples = 0;
	double sampleRate = 44100.0;
	bool isPlaying = false;
	bool isOffline = false;

	bool isValid() const noexcept
	{
		return numSamples > 0 && sampleRate > 0.0;
	}
};

struct ClipTransportState
{
	SamplePosition position = 0;
	int numSamples = 0;
	bool isPlaying = false;
};

struct ClipRenderDiagnostics
{
	std::atomic<int64_t> invalidContexts { 0 };
	std::atomic<int64_t> transportDiscontinuities { 0 };
	std::atomic<int64_t> dspResetRequests { 0 };
};

class ApexClipRenderCore final
{
public:
	void prepare(double sampleRate, int blockSize) noexcept
	{
		sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
		blockSize_ = juce::jmax(1, blockSize);
		lastTransport_ = {};
		hasLastTransport_ = false;
	}

	void releaseResources() noexcept
	{
		sampleRate_ = 44100.0;
		blockSize_ = 512;
		lastTransport_ = {};
		hasLastTransport_ = false;
	}

	ClipRenderContext makeContext(SamplePosition timelineSample,
								  int numSamples,
								  bool isPlaying,
								  bool isOffline) const noexcept
	{
		ClipRenderContext context;
		context.timelineSample = timelineSample;
		context.numSamples = numSamples;
		context.sampleRate = sampleRate_;
		context.isPlaying = isPlaying;
		context.isOffline = isOffline;
		return context;
	}

	bool validateContext(const ClipRenderContext& context) noexcept
	{
		if (context.isValid())
			return true;

		diagnostics_.invalidContexts.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	bool consumeTransportDiscontinuity(SamplePosition position, bool isPlaying, int numSamples) noexcept
	{
		const bool discontinuous = !hasLastTransport_
			? false
			: ((!lastTransport_.isPlaying && isPlaying)
				|| (lastTransport_.isPlaying && !isPlaying)
				|| (isPlaying && lastTransport_.isPlaying
					&& (position < lastTransport_.position
						|| std::abs((double)(position - lastTransport_.position - lastTransport_.numSamples)) > 1.0)));

		lastTransport_.position = position;
		lastTransport_.numSamples = numSamples;
		lastTransport_.isPlaying = isPlaying;
		hasLastTransport_ = true;

		if (discontinuous)
			diagnostics_.transportDiscontinuities.fetch_add(1, std::memory_order_relaxed);

		return discontinuous;
	}

	void noteDspResetRequest() noexcept
	{
		diagnostics_.dspResetRequests.fetch_add(1, std::memory_order_relaxed);
	}

	const ClipRenderDiagnostics& getDiagnostics() const noexcept { return diagnostics_; }

private:
	double sampleRate_ = 44100.0;
	int blockSize_ = 512;
	ClipTransportState lastTransport_;
	bool hasLastTransport_ = false;
	ClipRenderDiagnostics diagnostics_;
};

} // namespace DAW::SoundEngine
