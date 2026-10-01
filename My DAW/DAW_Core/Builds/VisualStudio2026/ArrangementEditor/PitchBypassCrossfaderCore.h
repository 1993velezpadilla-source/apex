// ============================================================================
// PitchBypassCrossfaderCore.h
// ----------------------------------------------------------------------------
// Nucleo: Smooth crossfade between the pitch-shifted DSP path and the
// straight bypass path when the smoothed pitch crosses zero.
// ============================================================================

#pragma once

#include <cmath>

namespace ArrangementEditor
{

class PitchBypassCrossfaderCore
{
public:
    enum class Mode { PitchPath, NormalPath };

    void prepare(double sampleRate, double crossfadeSeconds = 0.010) noexcept
    {
        sr = (sampleRate > 0.0) ? sampleRate : 44100.0;
        xfSec = (crossfadeSeconds > 0.0) ? crossfadeSeconds : 0.010;
        recomputeRampSamples();
        pitchGain = 1.0f;
        normalGain = 0.0f;
        targetMode = Mode::PitchPath;
        remaining = 0;
    }

    void reset(Mode mode = Mode::PitchPath) noexcept
    {
        targetMode = mode;
        remaining = 0;
        pitchGain = mode == Mode::PitchPath ? 1.0f : 0.0f;
        normalGain = mode == Mode::NormalPath ? 1.0f : 0.0f;
    }

    void getNextGains(float& outPitchGain, float& outNormalGain) noexcept
    {
        if (remaining > 0)
        {
            const float pos = 1.0f - (static_cast<float>(remaining) / static_cast<float>(rampSamples));
            const float a = std::sin(pos * 1.5707963f);
            const float b = std::cos(pos * 1.5707963f);

            if (targetMode == Mode::NormalPath)
            {
                normalGain = a;
                pitchGain = b;
            }
            else
            {
                pitchGain = a;
                normalGain = b;
            }

            if (--remaining == 0)
            {
                if (targetMode == Mode::NormalPath) { normalGain = 1.0f; pitchGain = 0.0f; }
                else                                { pitchGain = 1.0f; normalGain = 0.0f; }
            }
        }

        outPitchGain = pitchGain;
        outNormalGain = normalGain;
    }

    void requestMode(Mode m) noexcept
    {
        if (m == targetMode && remaining == 0)
            return;
        if (m == targetMode)
            return;

        targetMode = m;
        remaining = rampSamples;
    }

    void updateFromSmoother(bool smootherIsAtZero) noexcept
    {
        requestMode(smootherIsAtZero ? Mode::NormalPath : Mode::PitchPath);
    }

    bool needsPitchPath() const noexcept { return pitchGain > 0.0f || remaining > 0; }
    bool needsNormalPath() const noexcept { return normalGain > 0.0f || remaining > 0; }
    bool isCrossfading() const noexcept { return remaining > 0; }

private:
    void recomputeRampSamples() noexcept
    {
        const double n = sr * xfSec;
        rampSamples = (n > 1.0) ? static_cast<int>(n) : 1;
    }

    double sr { 44100.0 };
    double xfSec { 0.010 };
    int rampSamples { 441 };
    int remaining { 0 };

    float pitchGain { 1.0f };
    float normalGain { 0.0f };
    Mode targetMode { Mode::PitchPath };
};

} // namespace ArrangementEditor
