#pragma once
#include <JuceHeader.h>

namespace DAW::SoundEngine {

/**
 *  Ring-buffer PDC delay line.
 *
 *  C7 (low-buffer hardening): delay changes are crossfaded, never stepped.
 *  The crossfade is driven by the EFFECTIVE read delay: any change between
 *  consecutive read() calls (PDC resync, monitoring-PDC bypass toggle,
 *  latency update) arms a ~5 ms equal-gain crossfade from the previous tap
 *  to the new tap, so compensation changes cannot produce a hard
 *  discontinuity. First use after prepare() starts cleanly from silence —
 *  no fade needed. Everything here is allocation-free and noexcept.
 */
class ApexPdcDelayLineCore final
{
public:
	void prepare(int capacity)
	{
		buffer_.setSize(2, juce::jmax(1, capacity));
		buffer_.clear();
		writePos_ = 0;
		primed_ = 0;
		currentReadDelay_ = -1;
		previousReadDelay_ = 0;
		fadeRemaining_ = 0;
	}

	/** Arm the delay-change crossfade duration (samples). Call with
	 *  ~0.005 * sampleRate; idempotent. */
	void setCrossfadeSamples(int fadeSamples) noexcept
	{
		fadeSamples_ = juce::jmax(0, fadeSamples);
	}

	void push(const float* left, const float* right, int numSamples) noexcept
	{
		if (left == nullptr || right == nullptr || numSamples <= 0 || buffer_.getNumSamples() <= 0)
			return;

		const int capacity = buffer_.getNumSamples();
		auto* writeLeft = buffer_.getWritePointer(0);
		auto* writeRight = buffer_.getWritePointer(1);

		for (int i = 0; i < numSamples; ++i)
		{
			writeLeft[writePos_] = left[i];
			writeRight[writePos_] = right[i];
			writePos_ = (writePos_ + 1) % capacity;
		}
		primed_ = juce::jmin(capacity, primed_ + numSamples);
	}

	/** Read with an explicit effective delay (per-block overrides used by
	 *  monitoring-PDC Bypass/Reduced modes). Any effective-delay change
	 *  after the line has history arms the crossfade. */
	void read(float* outLeft, float* outRight, int numSamples, int effectiveDelay) noexcept
	{
		if (outLeft == nullptr || outRight == nullptr || numSamples <= 0 || buffer_.getNumSamples() <= 0)
			return;

		const int capacity = buffer_.getNumSamples();
		effectiveDelay = juce::jlimit(0, juce::jmax(0, capacity - numSamples - 1), effectiveDelay);

		if (currentReadDelay_ < 0)
		{
			currentReadDelay_ = effectiveDelay;   // first use — start clean, no fade
		}
		else if (effectiveDelay != currentReadDelay_)
		{
			if (primed_ > 0 && fadeSamples_ > 0)
			{
				previousReadDelay_ = currentReadDelay_;
				fadeRemaining_ = fadeSamples_;
			}
			currentReadDelay_ = effectiveDelay;
		}

		const auto* readLeft = buffer_.getReadPointer(0);
		const auto* readRight = buffer_.getReadPointer(1);

		for (int i = 0; i < numSamples; ++i)
		{
			const int posNew = (writePos_ - currentReadDelay_ - numSamples + i + capacity * 4) % capacity;
			if (fadeRemaining_ > 0)
			{
				const int posOld = (writePos_ - previousReadDelay_ - numSamples + i + capacity * 4) % capacity;
				const float t = 1.0f - (float) fadeRemaining_ / (float) fadeSamples_;
				outLeft[i]  = readLeft[posOld]  * (1.0f - t) + readLeft[posNew]  * t;
				outRight[i] = readRight[posOld] * (1.0f - t) + readRight[posNew] * t;
				--fadeRemaining_;
			}
			else
			{
				outLeft[i]  = readLeft[posNew];
				outRight[i] = readRight[posNew];
			}
		}
	}

	/** Backward-compatible read at the stored delay. */
	void read(float* outLeft, float* outRight, int numSamples) noexcept
	{
		read(outLeft, outRight, numSamples, delaySamples_);
	}

	int getCapacity() const noexcept { return buffer_.getNumSamples(); }
	int getDelaySamples() const noexcept { return delaySamples_; }
	void setDelaySamples(int delaySamples) noexcept { delaySamples_ = juce::jmax(0, delaySamples); }

private:
	juce::AudioBuffer<float> buffer_;
	int writePos_ = 0;
	int delaySamples_ = 0;
	int primed_ = 0;             // samples of valid history written (capped at capacity)
	int currentReadDelay_ = -1;  // effective delay used by the previous read (-1 = none yet)
	int previousReadDelay_ = 0;  // tap being faded away from
	int fadeRemaining_ = 0;      // samples left in the active crossfade
	int fadeSamples_ = 0;        // crossfade duration (0 = step, pre-legacy behaviour)
};

} // namespace DAW::SoundEngine
