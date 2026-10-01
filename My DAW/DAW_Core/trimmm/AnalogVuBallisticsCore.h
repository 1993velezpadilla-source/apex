#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace DAW {

/**
 * AnalogVuBallisticsCore
 *
 * Needle ballistics for the analog VU meter. Stateless w.r.t. audio data:
 * feed it linear-gain peak samples (typically max(L,R) read from the track's
 * InputMeterCore) and read back a smoothed needle value.
 *
 * Internal state is per-instance, so each panel showing a different track
 * holds its own smoothing — switching tracks does not bleed needle history.
 *
 * Three modes:
 *   ClassicVu : symmetric ~300 ms time constant (IEC 60268-17 reference VU)
 *   Ppm       : 10 ms rise / 1.7 s fall (BBC PPM)
 *   Custom    : user-defined rise/fall in seconds
 *
 * Peak hold (for the PEAK MAX readout) is tracked with infinite hold until
 * resetPeakHold() is called.
 */
class AnalogVuBallisticsCore
{
public:
    enum class Mode
    {
        ClassicVu,
        Ppm,
        Custom
    };

    AnalogVuBallisticsCore() noexcept { recomputeAlphas(); }

    void setMode(Mode m) noexcept
    {
        mode_ = m;
        recomputeAlphas();
    }

    Mode getMode() const noexcept { return mode_; }

    /** Custom rise/fall time constants in seconds. Only honoured in Custom mode. */
    void setCustomTimeConstants(float riseSec, float fallSec) noexcept
    {
        customRiseSec_ = juce::jmax(0.001f, riseSec);
        customFallSec_ = juce::jmax(0.001f, fallSec);
        recomputeAlphas();
    }

    /** UI tick rate. Affects alpha. Default 60 Hz. Call from prepare(). */
    void setTickRateHz(float hz) noexcept
    {
        tickRateHz_ = juce::jmax(1.0f, hz);
        recomputeAlphas();
    }

    /** Feed one linear-gain peak sample. Call from the UI tick. */
    void feed(float peakGain) noexcept
    {
        const float target = juce::jmax(0.0f, peakGain);

        if (target > smoothed_)
            smoothed_ += (target - smoothed_) * alphaRise_;
        else
            smoothed_ += (target - smoothed_) * alphaFall_;

        if (target > peakHold_)
            peakHold_ = target;
    }

    float getNeedleGain() const noexcept { return smoothed_; }

    float getNeedleDb() const noexcept
    {
        return smoothed_ <= 1.0e-6f ? -120.0f : 20.0f * std::log10(smoothed_);
    }

    float getPeakMaxGain() const noexcept { return peakHold_; }

    float getPeakMaxDb() const noexcept
    {
        return peakHold_ <= 1.0e-6f ? -120.0f : 20.0f * std::log10(peakHold_);
    }

    /** Reset peak max only — needle keeps its current smoothed position. */
    void resetPeakHold() noexcept { peakHold_ = 0.0f; }

    /** Reset everything. Call this when the panel is rebound to a new track. */
    void resetAll() noexcept
    {
        smoothed_ = 0.0f;
        peakHold_ = 0.0f;
    }

private:
    void recomputeAlphas() noexcept
    {
        const float dt = 1.0f / tickRateHz_;
        float riseSec = 0.3f;
        float fallSec = 0.3f;

        switch (mode_)
        {
            case Mode::ClassicVu: riseSec = fallSec = 0.3f; break;
            case Mode::Ppm:       riseSec = 0.010f; fallSec = 1.700f; break;
            case Mode::Custom:    riseSec = customRiseSec_; fallSec = customFallSec_; break;
        }

        alphaRise_ = 1.0f - std::exp(-dt / riseSec);
        alphaFall_ = 1.0f - std::exp(-dt / fallSec);
    }

    Mode  mode_           { Mode::ClassicVu };
    float tickRateHz_     { 60.0f };
    float customRiseSec_  { 0.3f };
    float customFallSec_  { 0.3f };
    float alphaRise_      { 0.0541f };
    float alphaFall_      { 0.0541f };
    float smoothed_       { 0.0f };
    float peakHold_       { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogVuBallisticsCore)
};

} // namespace DAW
