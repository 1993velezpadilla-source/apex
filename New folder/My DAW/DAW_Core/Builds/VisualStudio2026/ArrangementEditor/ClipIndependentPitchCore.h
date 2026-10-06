#pragma once
#include <JuceHeader.h>
#include <signalsmith-stretch.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

namespace ArrangementEditor {

struct ClipPitchProcessParams {
    double pitchSemitones = 0.0;
    double fineTuneCents = 0.0;
    double formantSemitones = 0.0;
    bool preserveFormants = false;
    int sampleRate = 44100;
    int channels = 2;
    // Optional per-sample pitch ramp (semitones). When non-null, processBlock
    // updates targetFactor_ from rampData[s] each sample instead of using the
    // fixed pitchSemitones scalar — eliminates the block-boundary zipper artefact
    // on the non-demon ClipIndependentPitchCore path (Bug 40).
    const float* pitchRampData = nullptr;
    int pitchRampLength = 0;

    // Time stretch ratio. 1.0 = no stretch, >1.0 = clip longer,
    // <1.0 = clip shorter. SignalSmith handles this via asymmetric
    // input/output buffer sizes.
    double stretchRatio = 1.0;

    double totalSemitones() const { return pitchSemitones + fineTuneCents / 100.0; }
    double pitchRatio() const { return std::pow(2.0, totalSemitones() / 12.0); }
    bool isActive() const { return std::abs(totalSemitones()) > 0.001; }
    bool isStretchActive() const { return std::abs(stretchRatio - 1.0) > 0.001; }
    bool needsProcessing() const { return isActive() || isStretchActive(); }
};

// ============================================================================
// ClipIndependentPitchCore — SignalSmith Stretch engine.
//
// Drop-in replacement for the previous STFT custom engine.
// Public interface is identical; only the DSP internals changed.
// Threading contract: audio thread only.
// ============================================================================
class ClipIndependentPitchCore {
public:
    ClipIndependentPitchCore() = default;

    void prepare(double newSampleRate, int newMaxBlockSize, int newMaxChannels)
    {
        sampleRate_    = juce::jmax(1.0, newSampleRate);
        maxBlockSize_  = juce::jmax(1, newMaxBlockSize);
        maxChannels_   = juce::jmin(newMaxChannels, kMaxChannels);

        smoothingCoeff_   = computeSmoothingCoefficient();
        targetFactor_     = 1.0;
        smoothedFactor_   = 1.0;
        currentSemitones_ = 0.0;

        // scratchIn must hold up to maxBlockSize_ * 10 (worst case stretch=0.1)
        const size_t maxInputSize = (size_t)(maxBlockSize_ * 10);

        stereoStretcher_.presetDefault(maxChannels_, static_cast<float>(sampleRate_));
        lastChans_ = maxChannels_;
        for (int ch = 0; ch < maxChannels_; ++ch)
        {
            scratchIn_[ch].resize(maxInputSize, 0.0f);
            scratchOut_[ch].resize((size_t)maxBlockSize_, 0.0f);
        }

        processingActive_  = false;
        bypassTailSamples_ = 0;
    }

    void reset()
    {
        stereoStretcher_.reset();
        for (int ch = 0; ch < maxChannels_; ++ch)
        {
            std::fill(scratchIn_[ch].begin(),  scratchIn_[ch].end(),  0.0f);
            std::fill(scratchOut_[ch].begin(), scratchOut_[ch].end(), 0.0f);
        }
        targetFactor_      = 1.0;
        smoothedFactor_    = 1.0;
        currentSemitones_  = 0.0;
        processingActive_  = false;
        bypassTailSamples_ = 0;
    }

    void processBlock(const float* const* input,
                      float* const*       output,
                      int numChannels,
                      int numSamples,
                      const ClipPitchProcessParams& params)
    {
        const int chans = juce::jmin(numChannels, maxChannels_);
        if (chans != lastChans_)
        {
            stereoStretcher_.presetDefault(chans, static_cast<float>(sampleRate_));
            stereoStretcher_.reset();
            lastChans_ = chans;
            processingActive_ = false;
        }

        targetFactor_ = params.isActive() ? params.pitchRatio() : 1.0;
        const bool targetIsUnity   = isNearUnity(targetFactor_);
        const bool smoothedIsUnity = isNearUnity(smoothedFactor_);
        const bool stretchIsUnity  = !params.isStretchActive();

        // Fast bypass: neither pitch nor stretch active
        if (!processingActive_ && targetIsUnity && smoothedIsUnity && stretchIsUnity)
        {
            for (int ch = 0; ch < chans; ++ch)
                if (input[ch] != output[ch])
                    std::memcpy(output[ch], input[ch], sizeof(float) * (size_t)numSamples);
            return;
        }

        if (!processingActive_)
        {
            processingActive_ = true;
            for (int ch = 0; ch < chans; ++ch)
            {
                std::fill(scratchIn_[ch].begin(), scratchIn_[ch].end(), 0.0f);
                std::fill(scratchOut_[ch].begin(), scratchOut_[ch].end(), 0.0f);
            }
            stereoStretcher_.reset();
        }

        // ── Smoother chain (Bug 40 fix preserved) ────────────────────────────
        // When a per-sample ramp arrives from PitchSmootherCore it is already
        // interpolated — bypass the internal 50ms smoother to avoid double-lag.
        const bool useRamp = (params.pitchRampData != nullptr
                              && params.pitchRampLength >= numSamples);

        const bool pitchInTransition = std::abs(targetFactor_ - smoothedFactor_) > 1.0e-3;
        if (!pitchInTransition)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                const double targetSt = useRamp
                    ? (double)params.pitchRampData[s] + params.fineTuneCents / 100.0
                    : params.totalSemitones();
                const double coeff = useRamp ? 1.0 : smoothingCoeff_;
                const double targetFac = std::pow(2.0, targetSt / 12.0);
                smoothedFactor_ += (targetFac - smoothedFactor_) * coeff;
            }
            currentSemitones_ = 12.0 * std::log2(juce::jmax(1e-6, smoothedFactor_));
        }

        // ── SignalSmith processing ────────────────────────────────────────────
        const double stretchRatio = juce::jmax(0.01, params.stretchRatio);
        const int inputSamples = juce::jmax(1,
            (int) std::llround((double) numSamples / stretchRatio));
        const int safeInputSamples = juce::jmin(inputSamples, (int) scratchIn_[0].size());

        if (pitchInTransition)
        {
            processSubBlocks(input, output, chans, numSamples, safeInputSamples, params, useRamp);
        }
        else
        {
            stereoStretcher_.setTransposeSemitones(static_cast<float>(currentSemitones_));
            const float* inArrays[kMaxChannels] = {};
            float* outArrays[kMaxChannels] = {};
            for (int ch = 0; ch < chans; ++ch)
            {
                std::copy(input[ch], input[ch] + safeInputSamples, scratchIn_[ch].begin());
                inArrays[ch] = scratchIn_[ch].data();
                outArrays[ch] = scratchOut_[ch].data();
            }

            stereoStretcher_.process(inArrays, safeInputSamples, outArrays, numSamples);
            for (int ch = 0; ch < chans; ++ch)
                std::copy(scratchOut_[ch].begin(), scratchOut_[ch].begin() + numSamples, output[ch]);
        }

        // ── Deactivation when pitch returns to zero ───────────────────────────
        if (isNearUnity(smoothedFactor_))
        {
            if (bypassTailSamples_ <= 0)
                bypassTailSamples_ = latencySamples();
            else
            {
                --bypassTailSamples_;
                if (bypassTailSamples_ == 0)
                {
                    processingActive_ = false;
                    stereoStretcher_.reset();
                }
            }
        }
        else
        {
            bypassTailSamples_ = 0;
        }
    }

    int latencySamples() const noexcept
    {
        return stereoStretcher_.inputLatency() + stereoStretcher_.outputLatency();
    }

    void processSubBlocks(const float* const* input, float* const* output, int chans,
                          int numSamples, int safeInputSamples,
                          const ClipPitchProcessParams& params, bool useRamp)
    {
        constexpr int kPitchSubBlock = 32;
        const double stretchRatio = juce::jmax(0.01, params.stretchRatio);
        int produced = 0;
        while (produced < numSamples)
        {
            const int chunk = juce::jmin(kPitchSubBlock, numSamples - produced);
            for (int s = 0; s < chunk; ++s)
            {
                const int rampIndex = produced + s;
                const double targetSt = useRamp
                    ? (double)params.pitchRampData[rampIndex] + params.fineTuneCents / 100.0
                    : params.totalSemitones();
                const double coeff = useRamp ? 1.0 : smoothingCoeff_;
                const double targetFac = std::pow(2.0, targetSt / 12.0);
                smoothedFactor_ += (targetFac - smoothedFactor_) * coeff;
            }
            currentSemitones_ = 12.0 * std::log2(juce::jmax(1e-6, smoothedFactor_));
            stereoStretcher_.setTransposeSemitones(static_cast<float>(currentSemitones_));

            const int subInputSamples = juce::jmax(1, (int)std::llround((double)chunk / stretchRatio));
            const int inputOffset = juce::jlimit(0, juce::jmax(0, safeInputSamples - subInputSamples),
                                                (int)std::llround((double)produced / stretchRatio));
            const float* inArrays[kMaxChannels] = {};
            float* outArrays[kMaxChannels] = {};
            for (int ch = 0; ch < chans; ++ch)
            {
                std::copy(input[ch] + inputOffset, input[ch] + inputOffset + subInputSamples, scratchIn_[ch].begin());
                inArrays[ch] = scratchIn_[ch].data();
                outArrays[ch] = scratchOut_[ch].data();
            }
            stereoStretcher_.process(inArrays, subInputSamples, outArrays, chunk);
            for (int ch = 0; ch < chans; ++ch)
                std::copy(scratchOut_[ch].begin(), scratchOut_[ch].begin() + chunk, output[ch] + produced);
            produced += chunk;
        }
    }

    double getCurrentPitchFactor()    const noexcept { return smoothedFactor_; }
    double getCurrentPitchSemitones() const noexcept { return currentSemitones_; }

private:
    static constexpr int kMaxChannels = 8;

    bool isNearUnity(double factor) const noexcept
    {
        return std::abs(factor - 1.0) < 1.0e-4;
    }

    double computeSmoothingCoefficient() const noexcept
    {
        constexpr double tau = 0.050;
        return 1.0 - std::exp(-1.0 / juce::jmax(1.0, sampleRate_ * tau));
    }

    double sampleRate_       = 44100.0;
    int    maxBlockSize_     = 512;
    int    maxChannels_      = 2;
    double targetFactor_     = 1.0;
    double smoothedFactor_   = 1.0;
    double smoothingCoeff_   = 1.0;
    double currentSemitones_ = 0.0;
    int    bypassTailSamples_= 0;
    bool   processingActive_ = false;

    // SignalSmith Stretch — one multi-channel instance keeps stereo grains phase-locked.
    mutable signalsmith::stretch::SignalsmithStretch<float> stereoStretcher_;
    int lastChans_ = 0;

    // Scratch buffers (SignalSmith requires distinct in/out pointers)
    std::vector<float> scratchIn_[kMaxChannels];
    std::vector<float> scratchOut_[kMaxChannels];
};

} // namespace ArrangementEditor
