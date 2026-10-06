// ===========================================================================
// ClipPitchRenderPathCore.h
// Single official clip pitch render path.
// ===========================================================================
#pragma once
#include "DemonAtmosphereCore.h"
#include "DarkVoiceBodyCore.h"
#include "FormantShiftCore.h"
#include "IndependentPitchEngineCore.h"
#include "PitchBypassCrossfaderCore.h"
#include "PitchEngineBlendCore.h"
#include "PitchForensicAuditCore.h"
#include "PitchSmootherCore.h"
#include "TapeResamplePitchEngineCore.h"
#include "UnifiedPitchStateCore.h"
#include "VoiceTransformMapperCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{

struct ClipPitchRenderPathRequest
{
    const float* sourceData = nullptr;
    int sourceTotalSamples = 0;
    int64_t sourceStartSample = 0;
    int64_t sourceEndSample = 0;
    float* outputData = nullptr;
    int numOutputSamples = 0;
    int channel = 0;
    TimePitchState state;
    double outputSampleRate = 44100.0;
    bool offline = false;
};

class ClipPitchRenderPathCore
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        maxBlockSize_ = std::max(1, maxBlockSize);
        stateCore_.prepare(sampleRate_);
        independentA_.prepare(sampleRate_, maxBlockSize_, TimePitchQuality::Realtime);
        independentB_.prepare(sampleRate_, maxBlockSize_, TimePitchQuality::Realtime);
        tape_.prepare(sampleRate_, maxBlockSize_);
        formant_.prepare(sampleRate_, maxBlockSize_);
        darkBody_.prepare(sampleRate_, maxBlockSize_);
        atmosphere_.prepare(sampleRate_, maxBlockSize_);
        blender_.prepare(sampleRate_, maxBlockSize_);
        pitchSmoother_.prepare(sampleRate_, 0.030);
        pitchBypass_.prepare(sampleRate_, 0.010);
        dryBuffer_.setSize(1, maxBlockSize_, false, true, true);
        independentBuffer_.setSize(1, maxBlockSize_, false, true, true);
        independentIncomingBuffer_.setSize(1, maxBlockSize_, false, true, true);
        tapeBuffer_.setSize(1, maxBlockSize_, false, true, true);
        pitchWetBuffer_.setSize(1, maxBlockSize_, false, true, true);
    }

    void reset()
    {
        stateCore_.resetSmoothing(stateCore_.getTargetPitchSemitones());
        independentA_.reset();
        independentB_.reset();
        tape_.reset();
        formant_.reset();
        darkBody_.reset();
        atmosphere_.reset();
        blender_.reset();
        pitchSmoother_.reset(0.0f);
        pitchBypass_.reset(PitchBypassCrossfaderCore::Mode::PitchPath);
        activeIndependentIndex_ = 0;
        independentCrossfade_ = 1.0f;
        activeIndependentSemis_ = 0.0f;
        incomingIndependentSemis_ = 0.0f;
    }

    void setState(const TimePitchState& state)
    {
        currentState_ = state;

        TimePitchState effectiveState = state;
        if (effectiveState.voiceTransform.isActive())
        {
            const auto targets = VoiceTransformMapperCore::map(effectiveState.voiceTransform, 0.0f);
            effectiveState.pitchSemitones = PitchScaleMathCore::clampPitch(
                effectiveState.pitchSemitones + targets.pitchSemitones, true);
        }

        const double effectivePitchSemitones = effectiveState.totalPitchSemitones();
        pitchSmoother_.setPitchTargetSemitones(static_cast<float>(effectivePitchSemitones));
        stateCore_.setStretchRatio(effectiveState.stretchRatio);
        stateCore_.setPitchEngineVersion(effectiveState.pitchEngineVersion);
    }

    void render(const ClipPitchRenderPathRequest& req)
    {
        juce::ScopedNoDenormals noDenormals;
        if (req.outputData == nullptr || req.numOutputSamples <= 0)
            return;

        // Guard all internal single-channel scratch buffers against overflow if
        // the host increases its block size after prepare() was last called.
        const int needed = req.numOutputSamples;
        auto ensureBuf = [needed](juce::AudioBuffer<float>& b)
        {
            if (b.getNumSamples() < needed)
                b.setSize(1, needed, false, true, true);
        };
        ensureBuf(dryBuffer_);
        ensureBuf(pitchWetBuffer_);
        ensureBuf(independentBuffer_);
        ensureBuf(independentIncomingBuffer_);
        ensureBuf(tapeBuffer_);

        const int safeSamples = req.numOutputSamples;

        setState(req.state);

        const float smoothedPitch = pitchSmoother_.getNextSemitones();
        stateCore_.setPitchSemitones(smoothedPitch);

        auto snapshot = stateCore_.advanceAndGetSnapshot(safeSamples);
        audit_.markUnified();

        pitchBypass_.updateFromSmoother(std::abs(pitchSmoother_.getCurrentSemitones()) <= 1.0e-4f);

        float* dry = dryBuffer_.getWritePointer(0);
        float* pitchWet = pitchWetBuffer_.getWritePointer(0);
        float* independent = independentBuffer_.getWritePointer(0);
        float* tape = tapeBuffer_.getWritePointer(0);
        copyDry(req, dry, safeSamples);
        juce::FloatVectorOperations::clear(pitchWet, safeSamples);
        juce::FloatVectorOperations::clear(independent, safeSamples);
        juce::FloatVectorOperations::clear(tape, safeSamples);

        const bool renderPitchPath = pitchBypass_.needsPitchPath() && snapshot.wet > 0.0001;
        if (renderPitchPath && snapshot.independentBlend > 0.0001)
        {
            renderIndependentHopBlocks(req, independent, safeSamples, snapshot);
            audit_.markIndependent();
        }

        if (renderPitchPath && snapshot.tapeBlend > 0.0001)
        {
            tape_.setPitchScale(snapshot.pitchScale);
            tape_.setWailAmount(snapshot.wailAmount);
            tape_.renderSegment(req.sourceData, req.sourceTotalSamples,
                                req.sourceStartSample, req.sourceEndSample,
                                tape, safeSamples, req.channel);
            audit_.markTape();
            if (snapshot.wailAmount >= 0.02)
                audit_.markWail();
        }

        if (renderPitchPath)
            blender_.blend(dry, independent, tape, pitchWet, safeSamples, snapshot, &audit_);

        if (renderPitchPath && std::abs(snapshot.formantScale - 1.0) > 0.01)
        {
            formant_.process(pitchWet, safeSamples, req.channel, snapshot);
            audit_.markFormantShift();
        }

        if (renderPitchPath && snapshot.bodyAmount >= 0.05)
        {
            darkBody_.process(pitchWet, safeSamples, req.channel, snapshot);
            audit_.markDarkBody();
        }

        const bool renderNormalPath = pitchBypass_.needsNormalPath();
        if (renderNormalPath)
            audit_.markBypass();

        for (int i = 0; i < safeSamples; ++i)
        {
            float pitchGain = 0.0f;
            float normalGain = 0.0f;
            pitchBypass_.getNextGains(pitchGain, normalGain);
            req.outputData[i] = pitchWet[i] * pitchGain + dry[i] * normalGain;
        }

        // Atmosphere (crypt reverb + echo tap + howl noise) removed — pure dark pitch only.

        if (snapshot.zone == UnifiedPitchZone::DarkExtreme)
            audit_.markDarkExtreme();
        else if (snapshot.zone == UnifiedPitchZone::ChipmunkExtreme)
            audit_.markChipmunkExtreme();

        reportedLatencySamples_ = calculateLatency(snapshot);
        audit_.setReportedLatencySamples(reportedLatencySamples_);
    }

    int getReportedLatencySamples() const noexcept { return reportedLatencySamples_; }
    PitchForensicAuditCore& audit() noexcept { return audit_; }

    bool isPitchTransitionActive() const noexcept
    {
        return std::abs(pitchSmoother_.getCurrentSemitones()) > 1.0e-4f
            || pitchBypass_.isCrossfading();
    }

private:
    static float readSafe(const ClipPitchRenderPathRequest& req, int64_t index) noexcept
    {
        return (req.sourceData != nullptr && index >= 0 && index < req.sourceTotalSamples) ? req.sourceData[index] : 0.0f;
    }

    static void copyDry(const ClipPitchRenderPathRequest& req, float* dst, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            dst[i] = readSafe(req, req.sourceStartSample + i);
    }

    void renderIndependentHopBlocks(const ClipPitchRenderPathRequest& req,
                                    float* dst,
                                    int numSamples,
                                    const UnifiedPitchSnapshot& baseSnapshot)
    {
        constexpr int kPitchHopSamples = 1024;

        // Bug 28 fix: guard the incoming scratch buffer against overflow.
        // If numSamples grew beyond maxBlockSize_ since prepare() was last
        // called (e.g. the host changed its block size), resize before use.
        if (independentIncomingBuffer_.getNumSamples() < numSamples)
            independentIncomingBuffer_.setSize(1, numSamples, false, true, true);
        float* incoming = independentIncomingBuffer_.getWritePointer(0);

        for (int offset = 0; offset < numSamples; offset += kPitchHopSamples)
        {
            const int n = std::min(kPitchHopSamples, numSamples - offset);

            // Consume all n smoother values; keep the last as the settled pitch.
            float hopPitch = pitchSmoother_.getNextSemitones();
            for (int i = 1; i < n; ++i)
                hopPitch = pitchSmoother_.getNextSemitones();

            if (independentCrossfade_ >= 1.0f)
            {
                const float deltaSemis = std::abs(hopPitch - activeIndependentSemis_);
                if (deltaSemis > 0.5f)
                {
                    incomingIndependentSemis_ = hopPitch;
                    independentCrossfade_ = 0.0f;
                }
                else
                {
                    activeIndependentSemis_ = hopPitch;
                }
            }

            TimePitchState activeState = makeIndependentState(activeIndependentSemis_, baseSnapshot);
            activeIndependent().setState(activeState, baseSnapshot.preserveFormants);
            // Bug 27 fix: pass sourceStartSample (not +offset) as the source
            // start for the segment. The WSOLA engine tracks its own read
            // position internally across calls — it does not restart from
            // sourceStartSample on every call. Passing sourceStartSample+offset
            // caused the engine to seek forward on each hop, creating a
            // discontinuity in the analysis window that manifested as a click
            // at every pitch-change crossfade event.
            activeIndependent().renderSegment(req.sourceData, req.sourceTotalSamples,
                                             req.sourceStartSample, req.sourceEndSample,
                                             dst + offset, n, req.channel);

            if (independentCrossfade_ < 1.0f)
            {
                TimePitchState incomingState = makeIndependentState(incomingIndependentSemis_, baseSnapshot);
                incomingIndependent().setState(incomingState, baseSnapshot.preserveFormants);
                incomingIndependent().renderSegment(req.sourceData, req.sourceTotalSamples,
                                                   req.sourceStartSample, req.sourceEndSample,
                                                   incoming, n, req.channel);

                mixIndependentCrossfade(dst + offset, incoming, n);
            }
        }
    }

    TimePitchState makeIndependentState(float semitones, const UnifiedPitchSnapshot& baseSnapshot) const
    {
        TimePitchState state = currentState_;
        state.pitchSemitones = semitones;
        state.fineTuneCents = 0.0;
        state.stretchRatio = baseSnapshot.stretchRatio;
        state.preserveFormants = baseSnapshot.preserveFormants;
        state.pitchEngineVersion = baseSnapshot.pitchEngineVersion;
        return state;
    }

    IndependentPitchEngineCore& activeIndependent() noexcept
    {
        return activeIndependentIndex_ == 0 ? independentA_ : independentB_;
    }

    IndependentPitchEngineCore& incomingIndependent() noexcept
    {
        return activeIndependentIndex_ == 0 ? independentB_ : independentA_;
    }

    void mixIndependentCrossfade(float* active, const float* incoming, int numSamples) noexcept
    {
        const float fadeRate = static_cast<float>(1.0 / (sampleRate_ * 0.050));

        for (int i = 0; i < numSamples; ++i)
        {
            independentCrossfade_ = std::min(1.0f, independentCrossfade_ + fadeRate);
            const float gActive = std::cos(independentCrossfade_ * 1.5707963f);
            const float gIncoming = std::sin(independentCrossfade_ * 1.5707963f);
            active[i] = active[i] * gActive + incoming[i] * gIncoming;
        }

        if (independentCrossfade_ >= 1.0f)
        {
            activeIndependentIndex_ = 1 - activeIndependentIndex_;
            activeIndependentSemis_ = incomingIndependentSemis_;
            independentCrossfade_ = 1.0f;
        }
    }

    int calculateLatency(const UnifiedPitchSnapshot& snapshot) const noexcept
    {
        const double ind = static_cast<double>((activeIndependentIndex_ == 0 ? independentA_ : independentB_).getCurrentLatencySamples());
        const double tap = static_cast<double>(tape_.getCurrentLatencySamples());
        const double atmo = static_cast<double>(atmosphere_.getCurrentLatencySamples());
        return static_cast<int>(std::lround(ind * snapshot.independentBlend + tap * snapshot.tapeBlend + atmo));
    }

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    int reportedLatencySamples_ = 0;
    TimePitchState currentState_;
    UnifiedPitchStateCore stateCore_;
    IndependentPitchEngineCore independentA_;
    IndependentPitchEngineCore independentB_;
    TapeResamplePitchEngineCore tape_;
    FormantShiftCore formant_;
    DarkVoiceBodyCore darkBody_;
    DemonAtmosphereCore atmosphere_;
    PitchEngineBlendCore blender_;
    PitchSmootherCore pitchSmoother_;
    PitchBypassCrossfaderCore pitchBypass_;
    PitchForensicAuditCore audit_;
    juce::AudioBuffer<float> dryBuffer_;
    juce::AudioBuffer<float> independentBuffer_;
    juce::AudioBuffer<float> independentIncomingBuffer_;
    juce::AudioBuffer<float> tapeBuffer_;
    juce::AudioBuffer<float> pitchWetBuffer_;
    int activeIndependentIndex_ = 0;
    float independentCrossfade_ = 1.0f;
    float activeIndependentSemis_ = 0.0f;
    float incomingIndependentSemis_ = 0.0f;
};

} // namespace ArrangementEditor
