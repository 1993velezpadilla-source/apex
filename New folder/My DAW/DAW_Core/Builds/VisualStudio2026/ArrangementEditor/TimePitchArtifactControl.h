// ===========================================================================
// TimePitchArtifactControl.h
// Anti-click, crossfade de modo, smoothing de knobs.
//
// POR QUÉ EXISTE:
//   1. Anti-click fades:
//      Al cortar un clip mid-playback (split, mute, stop), el audio puede
//      clicar. Un fade de salida de 64-256 samples lo elimina.
//
//   2. Crossfade de modo:
//      Cambiar de Resample a Stretch durante playback causa un glitch
//      porque el read position y el buffer state son incompatibles.
//      Un crossfade corto (512-2048 samples) suaviza la transición.
//
//   3. Knob smoother (zipper noise):
//      Cambios discretos de pitchSemitones o stretchRatio producen
//      zipper noise en el audio. Un 1-pole smoother lo elimina:
//        smoothed = alpha * smoothed + (1-alpha) * target
//        alpha = exp(-2π * cutoffHz / sampleRate)
//
// AUDIO THREAD SAFETY:
//   Todo procesamiento en audio thread con estado pre-alocado.
//   Estado de smoother es local por instancia (per-clip).
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <cmath>

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// AntiClickFader
// Short linear fade-in / fade-out to prevent clicks at segment boundaries.
// ---------------------------------------------------------------------------
class AntiClickFader
{
public:
    enum class Mode { FadeIn, FadeOut, Idle };

    AntiClickFader() = default;

    void prepare(double sampleRate)
    {
        // 3ms fade at given sample rate
        fadeLengthSamples_ = juce::jmax(64, (int)(sampleRate * 0.003));
    }

    void triggerFadeIn()  { mode_ = Mode::FadeIn;  pos_ = 0; }
    void triggerFadeOut() { mode_ = Mode::FadeOut; pos_ = 0; }
    void reset()          { mode_ = Mode::Idle;    pos_ = 0; }

    bool isActive() const { return mode_ != Mode::Idle; }

    // Apply fade to buffer in-place (single channel)
    void process(float* data, int numSamples)
    {
        if (mode_ == Mode::Idle) return;

        for (int i = 0; i < numSamples && pos_ < fadeLengthSamples_; ++i, ++pos_)
        {
            const float t = (float)pos_ / (float)fadeLengthSamples_;
            const float g = (mode_ == Mode::FadeIn) ? t : (1.f - t);
            data[i] *= g;
        }

        if (pos_ >= fadeLengthSamples_)
        {
            if (mode_ == Mode::FadeOut)
            {
                // Zero remaining samples
                for (int i = pos_ - fadeLengthSamples_; i < numSamples; ++i)
                    data[i] = 0.f;
            }
            mode_ = Mode::Idle;
        }
    }

    // Process AudioBuffer
    void process(juce::AudioBuffer<float>& buffer, int numSamples)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            process(buffer.getWritePointer(ch), numSamples);
    }

private:
    Mode mode_            = Mode::Idle;
    int  pos_             = 0;
    int  fadeLengthSamples_ = 128;
};

// ---------------------------------------------------------------------------
// ModeCrossfader
// Short crossfade buffer when switching TimePitchMode during playback.
// Stores the last N samples of the outgoing engine, fades them out while
// fading in the new engine output.
// ---------------------------------------------------------------------------
class ModeCrossfader
{
public:
    void prepare(double sampleRate, int maxBlockSize, int channels = 2)
    {
        // 10ms crossfade
        xfadeLen_ = juce::jmax(128, (int)(sampleRate * 0.010));
        tailBuf_.setSize(channels, xfadeLen_ + maxBlockSize);
        tailBuf_.clear();
        pos_     = 0;
        active_  = false;
    }

    void reset()
    {
        tailBuf_.clear();
        pos_    = 0;
        active_ = false;
    }

    // Call when mode is about to change. Captures current output as tail.
    void captureOutgoingTail(const juce::AudioBuffer<float>& lastOutput,
                              int numSamples)
    {
        const int copy = juce::jmin(numSamples, xfadeLen_);
        const int offset = numSamples - copy;
        for (int ch = 0; ch < juce::jmin(tailBuf_.getNumChannels(),
                                          lastOutput.getNumChannels()); ++ch)
            tailBuf_.copyFrom(ch, 0, lastOutput, ch, offset, copy);
        pos_    = 0;
        active_ = true;
    }

    // Mix crossfade tail into newOutput (call after new engine produces output)
    void mixCrossfade(juce::AudioBuffer<float>& newOutput, int numSamples)
    {
        if (!active_) return;

        const int channels = juce::jmin(tailBuf_.getNumChannels(),
                                         newOutput.getNumChannels());
        for (int i = 0; i < numSamples && pos_ < xfadeLen_; ++i, ++pos_)
        {
            const float t     = (float)pos_ / (float)xfadeLen_;
            const float fadeIn  = t;          // new engine: 0→1
            const float fadeOut = 1.f - t;    // old tail:   1→0

            for (int ch = 0; ch < channels; ++ch)
            {
                const float* tail = tailBuf_.getReadPointer(ch);
                float* out        = newOutput.getWritePointer(ch);
                out[i] = out[i] * fadeIn + tail[pos_] * fadeOut;
            }
        }

        if (pos_ >= xfadeLen_)
        {
            active_ = false;
            tailBuf_.clear();
        }
    }

    bool isActive() const { return active_; }

private:
    juce::AudioBuffer<float> tailBuf_;
    int  xfadeLen_ = 512;
    int  pos_      = 0;
    bool active_   = false;
};

// ---------------------------------------------------------------------------
// ParameterSmoother
// 1-pole IIR smoother for pitch, stretch, formant parameters.
// Eliminates zipper noise from discrete knob steps.
//
// Usage (audio thread):
//   smoother.setTarget(newPitch);
//   const double smoothed = smoother.process();  // call once per sample
// ---------------------------------------------------------------------------
class ParameterSmoother
{
public:
    void prepare(double sampleRate, double cutoffHz = 20.0)
    {
        // alpha = exp(-2π * cutoff / sr)
        alpha_    = (float)std::exp(-2.0 * juce::MathConstants<double>::pi
                                   * cutoffHz / sampleRate);
        current_  = target_.load();
    }

    void setTarget(double value)
    {
        target_.store(value);
    }

    // Advance by one sample and return smoothed value
    double process()
    {
        current_ = current_ * alpha_ + (float)target_.load() * (1.f - alpha_);
        return (double)current_;
    }

    // Advance by N samples, return final value
    double process(int numSamples)
    {
        const double target = target_.load();
        for (int i = 0; i < numSamples; ++i)
            current_ = current_ * alpha_ + (float)target * (1.f - alpha_);
        return (double)current_;
    }

    void snapToTarget()
    {
        current_ = (float)target_.load();
    }

    double getCurrent() const { return (double)current_; }
    bool   isSettled(double tol = 0.0001) const
    {
        return std::abs((double)current_ - target_.load()) < tol;
    }

private:
    std::atomic<double> target_ { 0.0 };
    float               current_ = 0.f;
    float               alpha_   = 0.99f;
};

// ---------------------------------------------------------------------------
// TimePitchArtifactController
// Per-clip bundle: anti-click + crossfader + smoothers for pitch/stretch.
// ---------------------------------------------------------------------------
class TimePitchArtifactController
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        antiClick_.prepare(sampleRate);
        crossfader_.prepare(sampleRate, maxBlockSize);
        pitchSmoother_.prepare(sampleRate, 15.0);   // 15 Hz cutoff (~4ms smoothing)
        stretchSmoother_.prepare(sampleRate, 10.0); // 10 Hz cutoff (~6ms smoothing)
        formantSmoother_.prepare(sampleRate, 15.0);
    }

    void reset()
    {
        antiClick_.reset();
        crossfader_.reset();
        pitchSmoother_.snapToTarget();
        stretchSmoother_.snapToTarget();
        formantSmoother_.snapToTarget();
    }

    AntiClickFader&   antiClick()   { return antiClick_; }
    ModeCrossfader&   crossfader()  { return crossfader_; }
    ParameterSmoother& pitchSmoother()   { return pitchSmoother_; }
    ParameterSmoother& stretchSmoother() { return stretchSmoother_; }
    ParameterSmoother& formantSmoother() { return formantSmoother_; }

private:
    AntiClickFader    antiClick_;
    ModeCrossfader    crossfader_;
    ParameterSmoother pitchSmoother_;
    ParameterSmoother stretchSmoother_;
    ParameterSmoother formantSmoother_;
};

} // namespace ArrangementEditor
