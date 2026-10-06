#pragma once
#include <JuceHeader.h>

namespace DAW::SoundEngine {

class ApexPdcDelayLineCore final
{
public:
	void prepare(int capacity)
	{
		buffer_.setSize(2, juce::jmax(1, capacity));
		buffer_.clear();
		writePos_ = 0;
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
	}

	void read(float* outLeft, float* outRight, int numSamples) const noexcept
	{
		if (outLeft == nullptr || outRight == nullptr || numSamples <= 0 || buffer_.getNumSamples() <= 0)
			return;

		const int capacity = buffer_.getNumSamples();
		const auto* readLeft = buffer_.getReadPointer(0);
		const auto* readRight = buffer_.getReadPointer(1);

		for (int i = 0; i < numSamples; ++i)
		{
			const int pos = (writePos_ - delaySamples_ - numSamples + i + capacity * 4) % capacity;
			outLeft[i] = readLeft[pos];
			outRight[i] = readRight[pos];
		}
	}

	int getCapacity() const noexcept { return buffer_.getNumSamples(); }
	int getDelaySamples() const noexcept { return delaySamples_; }
	void setDelaySamples(int delaySamples) noexcept { delaySamples_ = juce::jmax(0, delaySamples); }

private:
	juce::AudioBuffer<float> buffer_;
	int writePos_ = 0;
	int delaySamples_ = 0;
};

} // namespace DAW::SoundEngine
