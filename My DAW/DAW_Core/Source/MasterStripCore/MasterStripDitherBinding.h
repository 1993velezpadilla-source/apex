#pragma once
#include <JuceHeader.h>
#include "../MasterCore/MasterDitherCore.h"

namespace DAW {

/**
 * MasterStripDitherBinding
 *
 * Glue between the master strip's dither UI and the audio-thread
 * MasterDitherCore. Same pattern as MasterStripCeilingBinding.
 *
 * Threading: UI thread only.
 */
class MasterStripDitherBinding
{
public:
    MasterStripDitherBinding() = default;

    void setDither(MasterDitherCore* d) noexcept { dither_ = d; }
    bool isBound() const noexcept                { return dither_ != nullptr; }

    // ── Mode ─────────────────────────────────────────────────────────

    using Mode = MasterDitherCore::Mode;

    void setMode(Mode m)
    {
        if (dither_ != nullptr)
            dither_->setMode(m);
    }

    Mode getMode() const
    {
        return dither_ != nullptr ? dither_->getMode() : Mode::Off;
    }

    static juce::String labelForMode(Mode m)
    {
        switch (m)
        {
            case Mode::Off:          return "Off";
            case Mode::TPDF:         return "TPDF";
            case Mode::NoiseShaped:  return "Noise Shaped";
        }
        return "Off";
    }

    static juce::StringArray getAllModeLabels()
    {
        return { "Off", "TPDF", "Noise Shaped" };
    }

    static Mode modeFromIndex(int index)
    {
        switch (index)
        {
            case 0:  return Mode::Off;
            case 1:  return Mode::TPDF;
            case 2:  return Mode::NoiseShaped;
            default: return Mode::Off;
        }
    }

    static int indexFromMode(Mode m)
    {
        switch (m)
        {
            case Mode::Off:          return 0;
            case Mode::TPDF:         return 1;
            case Mode::NoiseShaped:  return 2;
        }
        return 0;
    }

    // ── Target bits ──────────────────────────────────────────────────

    void setTargetBits(int bits)
    {
        if (dither_ != nullptr)
            dither_->setTargetBits(bits);
    }

    int getTargetBits() const
    {
        return dither_ != nullptr ? dither_->getTargetBits() : 24;
    }

    /** Standard target bit depths for the dropdown. */
    static juce::Array<int> getCommonBitDepths()
    {
        return { 16, 20, 24 };
    }

    static juce::String labelForBits(int bits)
    {
        return juce::String(bits) + "-bit";
    }

private:
    MasterDitherCore* dither_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripDitherBinding)
};

} // namespace DAW
