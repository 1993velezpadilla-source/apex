// ============================================================================
// PitchSmootherCore.h
// ----------------------------------------------------------------------------
// Nucleo: Pitch parameter smoothing.
//
// Purpose:
//   Decouple UI-thread pitch knob writes from audio-thread DSP reads.
//   Eliminates zipper noise caused by raw, discrete setPitch() calls
//   originating from Windows mouse-drag messages.
//
// Threading contract:
//   - UI thread:    setPitchTargetSemitones() ONLY.
//   - Audio thread: getNextSemitones() / getNextRatio() per sample.
//   - No allocations, no locks, no STFT resets here.
//
// This module is intentionally isolated. It does NOT touch the pitch DSP,
// the routing graph, the bubblegum panel, or any UI state.
// ============================================================================

#pragma once

#include <JuceHeader.h>
#include "../../Source/UICore/ForensicAuditWindow.h"
#include <atomic>
#include <cmath>

namespace ArrangementEditor
{

class PitchSmootherCore
{
public:
    PitchSmootherCore() = default;
    ~PitchSmootherCore() = default;

    PitchSmootherCore(const PitchSmootherCore&) = delete;
    PitchSmootherCore& operator=(const PitchSmootherCore&) = delete;

    void prepare(double newSampleRate, double timeConstantSeconds = 0.030) noexcept
    {
        sr = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        tau = timeConstantSeconds > 0.0 ? timeConstantSeconds : 0.030;
        coef = static_cast<float>(std::exp(-1.0 / (sr * tau)));
        current = target.load(std::memory_order_relaxed);
    }

    void reset(float semitones = 0.0f) noexcept
    {
        target.store(semitones, std::memory_order_relaxed);
        current = semitones;
    }

    void setPitchTargetSemitones(float semitones) noexcept
    {
        const float prev = target.load(std::memory_order_relaxed);
        target.store(semitones, std::memory_order_relaxed);
        // Log only when value changes meaningfully — avoid flooding on every audio block.
        if (std::fabs(semitones - prev) > 0.005f)
        {
            juce::String msg;
            msg << "[SMOOTHER ENTRY] target=" << juce::String(semitones, 3)
                << " prev=" << juce::String(prev, 3);
            DBG(msg);
            DAW::ForensicAuditLogger::getInstance().addLog(msg);
        }
    }

    float getNextSemitones() noexcept
    {
        const float t = target.load(std::memory_order_relaxed);
        current = coef * current + (1.0f - coef) * t;
        if (std::fabs(current - t) < 1.0e-7f)
            current = t;
        return current;
    }

    float getNextRatio() noexcept
    {
        return std::exp2(getNextSemitones() * (1.0f / 12.0f));
    }

    float getCurrentSemitones() const noexcept { return current; }

private:
    std::atomic<float> target { 0.0f };
    float current { 0.0f };
    float coef { 0.998f };
    double sr { 44100.0 };
    double tau { 0.030 };
};

} // namespace ArrangementEditor
