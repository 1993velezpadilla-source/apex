#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace APEX::ParametricEQ
{

// APEX-owned nonlinear character stage.  This is intentionally independent
// from any third-party EQ implementation: three product modes, fixed storage,
// no locks, no allocation and an exactly transparent Pure mode.
enum class CharacterMode : std::uint8_t
{
    Pure = 0,
    Velvet,
    Heat
};

constexpr int kCharacterModeCount = 3;

inline const char* characterModeName (CharacterMode mode) noexcept
{
    switch (mode)
    {
        case CharacterMode::Pure:   return "Pure";
        case CharacterMode::Velvet: return "Velvet";
        case CharacterMode::Heat:   return "Heat";
    }
    return "Pure";
}

class CharacterCore
{
public:
    void prepare (double sampleRate, int channels) noexcept
    {
        sampleRate_ = std::isfinite (sampleRate) && sampleRate > 1.0
                    ? sampleRate : 44100.0;
        channels_ = std::clamp (channels, 1, 2);
        // Roughly 8 ms coefficient settling. The state itself is fixed-size
        // and audio-thread owned.
        smoothing_ = std::exp (-1.0 / std::max (1.0, sampleRate_ * 0.008));
        reset();
    }

    void reset() noexcept
    {
        drive_ = 1.0;
        wet_ = 0.0;
        targetDrive_ = 1.0;
        targetWet_ = 0.0;
        mode_ = CharacterMode::Pure;
    }

    void setMode (CharacterMode mode) noexcept
    {
        mode_ = sanitiseMode (mode);
        switch (mode_)
        {
            case CharacterMode::Pure:
                targetDrive_ = 1.0;
                targetWet_ = 0.0;
                break;
            case CharacterMode::Velvet:
                targetDrive_ = 1.30;
                targetWet_ = 0.22;
                break;
            case CharacterMode::Heat:
                targetDrive_ = 1.85;
                targetWet_ = 0.38;
                break;
        }
    }

    CharacterMode getMode() const noexcept { return mode_; }

    void process (float* const* channels, int numberOfChannels,
                  int numberOfSamples) noexcept
    {
        if (channels == nullptr || numberOfSamples <= 0)
            return;

        const int count = std::clamp (numberOfChannels, 0, channels_);
        if (count <= 0)
            return;

        // An already-settled Pure mode is a bit-transparent no-op.
        if (mode_ == CharacterMode::Pure && wet_ <= 1.0e-12
            && targetWet_ <= 1.0e-12)
            return;

        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            drive_ = smoothing_ * drive_ + (1.0 - smoothing_) * targetDrive_;
            wet_ = smoothing_ * wet_ + (1.0 - smoothing_) * targetWet_;
            if (std::abs (drive_ - targetDrive_) < 1.0e-10)
                drive_ = targetDrive_;
            if (std::abs (wet_ - targetWet_) < 1.0e-10)
                wet_ = targetWet_;

            const double normaliser = std::tanh (std::max (1.0, drive_));
            for (int channel = 0; channel < count; ++channel)
            {
                auto* data = channels[channel];
                if (data == nullptr)
                    continue;

                const double x = std::isfinite (data[sample])
                               ? static_cast<double> (data[sample]) : 0.0;
                // Odd-symmetric normalized transfer: no intentional DC
                // offset, unity at +/-1, bounded outside nominal range.
                const double shaped = normaliser > 1.0e-12
                                    ? std::tanh (drive_ * x) / normaliser
                                    : x;
                const double y = x + wet_ * (shaped - x);
                data[sample] = std::isfinite (y) ? static_cast<float> (y) : 0.0f;
            }
        }
    }

private:
    static CharacterMode sanitiseMode (CharacterMode mode) noexcept
    {
        const auto value = static_cast<int> (mode);
        return value >= 0 && value < kCharacterModeCount
             ? mode : CharacterMode::Pure;
    }

    double sampleRate_ = 44100.0;
    int channels_ = 2;
    double smoothing_ = 0.0;
    double drive_ = 1.0;
    double wet_ = 0.0;
    double targetDrive_ = 1.0;
    double targetWet_ = 0.0;
    CharacterMode mode_ = CharacterMode::Pure;
};

} // namespace APEX::ParametricEQ
