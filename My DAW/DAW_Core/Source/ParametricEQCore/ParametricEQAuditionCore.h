#pragma once

#include "ParametricEQBiquad.h"

#include <array>

namespace APEX::ParametricEQ
{

// Transient Band Solo/Audition runtime. One fixed single-band filter lane set
// owned exclusively by the audio thread. Audition is never a hosted parameter
// and is never serialized: lifecycle events and stale tokens cancel it.
class AuditionBand
{
public:
    // Full clear: configuration, coefficients and filter history.
    void reset() noexcept
    {
        settings = {};
        sourceShape = FilterShape::Bell;
        placement = ChannelPlacement::Stereo;
        morph = 0.0;
        lower = {};
        upper = {};
        clearHistory();
    }

    // History-only clear: used by configure() so the freshly designed
    // coefficients and settings survive a requested reset.
    void clearHistory() noexcept
    {
        for (auto& state : lowerState_) state.reset();
        for (auto& state : upperState_) state.reset();
    }

    // The audition filter is the band's exact topology. Gain-bearing shapes
    // use a fixed unit probe gain so audition describes the controlled
    // region, not the current gain amount. Cut/notch/band-pass/all-pass
    // shapes keep their exact transfer.
    void configure (const BandSettings& rawSettings, DesignMode mode,
                    double sampleRate, bool resetState) noexcept
    {
        const auto safeSettings = sanitise (rawSettings, sampleRate);
        sourceShape = safeSettings.shape;
        placement = safeSettings.placement;
        morph = 0.0;
        lower = {};
        upper = {};

        if (safeSettings.enabled && ! safeSettings.bypassed)
        {
            auto settings = safeSettings;
            switch (settings.shape)
            {
                case FilterShape::Bell:
                case FilterShape::LowShelf:
                case FilterShape::HighShelf:
                case FilterShape::Tilt:
                case FilterShape::FlatTilt:
                    settings.gainDb = kAuditionProbeGainDb;
                    break;
                case FilterShape::LowCut:
                case FilterShape::HighCut:
                case FilterShape::Notch:
                case FilterShape::BandPass:
                case FilterShape::AllPass:
                    break;
            }

            if (isCutShape (settings.shape))
            {
                const auto exactOrder = settings.slopeDbPerOctave / 6.0;
                const auto lowerOrder = std::clamp (
                    static_cast<int> (std::floor (exactOrder)), 0, kMaxCutOrder);
                const auto upperOrder = std::min (kMaxCutOrder, lowerOrder + 1);
                morph = upperOrder == lowerOrder
                      ? 0.0 : std::clamp (exactOrder - lowerOrder, 0.0, 1.0);
                const bool highPass = settings.shape == FilterShape::LowCut;
                lower = FilterDesigner::designButterworthCut (
                    highPass, lowerOrder, settings.frequencyHz, sampleRate, mode);
                upper = FilterDesigner::designButterworthCut (
                    highPass, upperOrder, settings.frequencyHz, sampleRate, mode);
            }
            else
            {
                lower = FilterDesigner::designBand (settings, sampleRate, mode);
            }
        }

        this->settings = safeSettings;
        // History reset must never erase the configuration or coefficients
        // assigned above.
        if (resetState)
            clearHistory();
    }

    bool isIdentity() const noexcept
    {
        return lower.isIdentity() && (morph <= 0.0 || upper.isIdentity());
    }

    float filterSample (int lane, float input) noexcept
    {
        auto& lowerState = lowerState_[static_cast<std::size_t> (lane)];
        const auto filteredLower = lowerState.process (input, lower);
        if (morph <= 0.0)
            return filteredLower;
        auto& upperState = upperState_[static_cast<std::size_t> (lane)];
        const auto filteredUpper = upperState.process (input, upper);
        const auto output = static_cast<double> (filteredLower)
                          + morph * (static_cast<double> (filteredUpper)
                                   - static_cast<double> (filteredLower));
        return std::isfinite (output)
             ? static_cast<float> (output)
             : (std::isfinite (input) ? input : 0.0f);
    }

    // The technically meaningful region for each shape family:
    //   - gain shapes: the unity-probe residual (region controlled by band);
    //   - cut/notch: the exact removed/attenuated signal (1 - H);
    //   - band-pass: the passed region itself;
    //   - all-pass: the half-difference region of the phase transition.
    float regionFor (float filtered, float original) const noexcept
    {
        switch (sourceShape)
        {
            case FilterShape::Bell:
            case FilterShape::LowShelf:
            case FilterShape::HighShelf:
            case FilterShape::Tilt:
            case FilterShape::FlatTilt:
                return filtered - original;
            case FilterShape::LowCut:
            case FilterShape::HighCut:
            case FilterShape::Notch:
                return original - filtered;
            case FilterShape::BandPass:
                return filtered;
            case FilterShape::AllPass:
                return 0.5f * (original - filtered);
        }
        return 0.0f;
    }

    static constexpr double kAuditionProbeGainDb = 20.0 * 0.3010299956639812;

    BandSettings settings {};
    FilterShape sourceShape = FilterShape::Bell;
    ChannelPlacement placement = ChannelPlacement::Stereo;
    CascadeCoefficients lower {};
    CascadeCoefficients upper {};
    double morph = 0.0;
    std::array<CascadeState, kMaxChannels> lowerState_ {};
    std::array<CascadeState, kMaxChannels> upperState_ {};
};

// Packed lock-free audition mailbox. Written by control threads, adopted once
// per block by the audio thread. A full queue is unnecessary: cancellation is
// encoded in the same word, so an overflow cannot lose an End.
struct AuditionCommand
{
    std::uint64_t bits = 0;

    static constexpr std::uint64_t kActive = std::uint64_t (1) << 63;
    static constexpr int kSequenceShift = 51;
    static constexpr int kTokenShift = 31;
    static constexpr int kBandShift = 26;

    static AuditionCommand begin (int band, std::uint32_t token,
                                  std::uint32_t sequence) noexcept
    {
        AuditionCommand command;
        command.bits = kActive
                     | (static_cast<std::uint64_t> (sequence & 0xFFF) << kSequenceShift)
                     | (static_cast<std::uint64_t> (token & 0xFFFFF) << kTokenShift)
                     | (static_cast<std::uint64_t> (band & 0x1F) << kBandShift);
        return command;
    }

    static AuditionCommand end (std::uint32_t token) noexcept
    {
        AuditionCommand command;
        command.bits = static_cast<std::uint64_t> (token & 0xFFFFF) << kTokenShift;
        return command;
    }

    bool isActive() const noexcept { return (bits & kActive) != 0; }
    int band() const noexcept
    {
        return static_cast<int> ((bits >> kBandShift) & 0x1F);
    }
    std::uint32_t token() const noexcept
    {
        return static_cast<std::uint32_t> ((bits >> kTokenShift) & 0xFFFFF);
    }
    std::uint32_t sequence() const noexcept
    {
        return static_cast<std::uint32_t> ((bits >> kSequenceShift) & 0xFFF);
    }
};

} // namespace APEX::ParametricEQ
