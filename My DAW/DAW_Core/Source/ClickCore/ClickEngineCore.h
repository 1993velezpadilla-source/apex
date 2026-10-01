#pragma once
#include <JuceHeader.h>
#include "../TransportCore/TransportController.h"
#include "ClickStateModel.h"
#include "ClickSoundBankCore.h"
#include "ClickPatternCore.h"
#include "ClickRoutingCore.h"
#include "CountInCore.h"

namespace DAW {

class ClickEngineCore
{
public:
    void setSubsystems(TransportController* transport, ClickStateModel* state)
    {
        transport_ = transport;
        state_     = state;
    }

    void prepare(double sampleRate, int blockSize)
    {
        sampleRate_ = sampleRate;
        soundBank_.prepare(sampleRate);
        pattern_.setSampleRate(sampleRate);
        countIn_.prepare(sampleRate);
        routing_.prepare(blockSize);
    }

    void releaseResources() {}

    void beginCountIn()
    {
        if (!state_ || !transport_) return;
        jassert(sampleRate_ > 0.0 && "sampleRate_ read before prepareToPlay");
        if (sampleRate_ <= 0.0) return;
        const int bars = (int) state_->getCountInBars();
        if (bars <= 0)
        {
            state_->countInActive.store(false, std::memory_order_release);
            return;
        }

        countIn_.begin(bars, transport_->getTempo(), state_->beatsPerBar.load(std::memory_order_relaxed));
        state_->countInActive.store(true, std::memory_order_release);
    }

    void cancelCountIn()
    {
        countIn_.cancel();
        if (state_)
            state_->countInActive.store(false, std::memory_order_release);
    }

    bool isCountInActive() const noexcept { return countIn_.isActive(); }
    float getCountInProgress() const noexcept { return countIn_.getProgress(); }

    bool processBlock(int64_t blockStartSamples, int numSamples)
    {
        routing_.clear(numSamples);
        routing_.carryTailIntoBlock();   // C9: finish truncated bursts across the boundary

        if (!transport_ || !state_) return false;

        pattern_.setTempo(transport_->getTempo());
        pattern_.setTimeSig(state_->beatsPerBar.load(std::memory_order_relaxed),
                            state_->beatUnit.load(std::memory_order_relaxed));

        const bool muted       = state_->muted.load(std::memory_order_relaxed);
        const auto activeMode  = state_->getActiveMode();
        const bool isPlaying   = transport_->isPlaying();
        const bool isRecording = transport_->isRecording();
        const bool countingIn  = countIn_.isActive();

        bool shouldClick = false;
        if (!muted)
        {
            if (countingIn)
                shouldClick = true;
            else
            {
                switch (activeMode)
                {
                    case ClickActiveMode::Off:            shouldClick = false; break;
                    case ClickActiveMode::OnDuringRecord: shouldClick = isRecording; break;
                    case ClickActiveMode::Always:         shouldClick = isPlaying || isRecording; break;
                }
            }
        }

        if (shouldClick)
        {
            constexpr int kMaxBeatsPerBlock = 16;
            ClickPatternCore::BeatHit hits[kMaxBeatsPerBlock];
            const int numHits = pattern_.findBeatsInBlock(blockStartSamples, numSamples, hits, kMaxBeatsPerBlock);
            const float gain = state_->volumeLinear.load(std::memory_order_relaxed);

            for (int i = 0; i < numHits; ++i)
            {
                const auto& sample = hits[i].isDownbeat ? soundBank_.getAccentSample() : soundBank_.getRegularSample();
                if (sample.empty()) continue;
                routing_.addClickAtOffset(hits[i].sampleOffsetInBlock, sample.data(), (int) sample.size(), gain);
            }
        }

        bool countInFinished = false;
        if (countingIn)
        {
            countInFinished = countIn_.processBlock(numSamples);
            if (countInFinished && state_)
                state_->countInActive.store(false, std::memory_order_release);
        }

        return countInFinished;
    }

    void sumIntoOutput(float* outL, float* outR, int numSamples, const float* gainRamp = nullptr) const noexcept
    {
        routing_.sumIntoOutput(outL, outR, numSamples, gainRamp);
    }

private:
    TransportController* transport_ = nullptr;
    ClickStateModel*     state_     = nullptr;

    double             sampleRate_ { 0.0 };
    ClickSoundBankCore soundBank_;
    ClickPatternCore   pattern_;
    ClickRoutingCore   routing_;
    CountInCore        countIn_;
};

} // namespace DAW
