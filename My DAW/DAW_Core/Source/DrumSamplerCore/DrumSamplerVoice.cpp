#include "DrumSamplerVoice.h"

namespace DAW {

void DrumSamplerVoice::start(const DrumPadConfig& pad, float velocity, int64_t samplePos) {
    pad_ = &pad;
    velocity_ = velocity;
    // Rate ratio captured at start: file rate / engine rate. A 44.1k pad at
    // a 96k engine advances ~0.459 source samples per output sample, which
    // preserves the pad's pitch and duration in seconds at any device rate.
    const double fileRate = (! pad.layers.empty() && pad.layers[0].sampleRate > 0)
        ? (double) pad.layers[0].sampleRate : 44100.0;
    rateRatio_ = (engineSampleRate_ > 0.0) ? fileRate / engineSampleRate_ : 1.0;
    srcPosition_ = (double) samplePos;
    active_ = true;
    envStage_ = EnvStage::Attack;
    envValue_ = 0.0f;
    envSampleCounter_ = 0;
}

void DrumSamplerVoice::stop() {
    envStage_ = EnvStage::Release;
    envSampleCounter_ = 0;
}

void DrumSamplerVoice::process(
    juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!active_ || !pad_ || pad_->layers.empty()) return;

    const auto& layer = pad_->layers[0];
    if (layer.numSamples <= 0) return;

    const float* sourceData = layer.audioData.getReadPointer(0);
    const float gainLinear = juce::Decibels::decibelsToGain(pad_->gainDb) * velocity_;

    for (int i = 0; i < numSamples; ++i) {
        advanceEnvelope(1);
        if (envStage_ == EnvStage::Idle) { active_ = false; return; }

        const int64_t i0 = (int64_t) srcPosition_;
        if (i0 >= layer.numSamples) { active_ = false; return; }

        // Linear interpolation at the fractional source position — the
        // smallest correct rate-ratio resampling for one-shot drum playback
        // (keeps pad pitch/speed correct at any engine rate). Allocation-,
        // lock- and I/O-free: realtime-safe per Brain §2.
        const float frac = (float) (srcPosition_ - (double) i0);
        const float s0 = sourceData[i0];
        const float s1 = (i0 + 1 < layer.numSamples) ? sourceData[i0 + 1] : s0;
        const float sample = (s0 + frac * (s1 - s0)) * gainLinear * envValue_;

        outputBuffer.addSample(0, startSample + i, sample);
        if (outputBuffer.getNumChannels() > 1)
            outputBuffer.addSample(1, startSample + i, sample);

        srcPosition_ += rateRatio_;
    }
}

void DrumSamplerVoice::advanceEnvelope(int numSamples) {
    if (!pad_) return;
    // Stage lengths derive from the ACTUAL engine rate (was hardcoded 44100).
    const float rate = (float) engineSampleRate_;
    const float attackSamples = pad_->attackMs * 0.001f * rate;
    const float decaySamples = pad_->decayMs * 0.001f * rate;
    const float releaseSamples = pad_->releaseMs * 0.001f * rate;

    envSampleCounter_ += numSamples;

    switch (envStage_) {
        case EnvStage::Attack:
            envValue_ = (attackSamples > 0) ? (float)envSampleCounter_ / attackSamples : 1.0f;
            if (envValue_ >= 1.0f) { envValue_ = 1.0f; envStage_ = EnvStage::Decay; envSampleCounter_ = 0; }
            break;
        case EnvStage::Decay: {
            float t = (decaySamples > 0) ? (float)envSampleCounter_ / decaySamples : 1.0f;
            envValue_ = 1.0f - (1.0f - pad_->sustainLevel) * juce::jmin(t, 1.0f);
            if (t >= 1.0f) { envStage_ = EnvStage::Sustain; envSampleCounter_ = 0; }
            break;
        }
        case EnvStage::Sustain:
            envValue_ = pad_->sustainLevel;
            break;
        case EnvStage::Release: {
            float t = (releaseSamples > 0) ? (float)envSampleCounter_ / releaseSamples : 1.0f;
            envValue_ *= (1.0f - juce::jmin(t, 1.0f));
            if (t >= 1.0f) { envValue_ = 0.0f; envStage_ = EnvStage::Idle; }
            break;
        }
        case EnvStage::Idle:
            break;
    }
}

} // namespace DAW
