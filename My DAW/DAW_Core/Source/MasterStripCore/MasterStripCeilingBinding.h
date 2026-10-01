#pragma once
#include <JuceHeader.h>
#include <functional>
#include "../MasterCore/MasterCeilingCore.h"

namespace DAW {

/**
 * MasterStripCeilingBinding
 *
 * Glue between the master strip's ceiling UI controls and the audio-thread
 * MasterCeilingCore. UI controls call setMode/setCeilingDb on this binding,
 * and the binding pushes those values into the core via its atomic setters.
 * UI controls read getMode/getCeilingDb/getGainReductionDb from this binding,
 * which simply forwards to the core's atomic getters.
 *
 * No state lives here — this is pure indirection. The reason for a binding
 * layer instead of UI talking to the core directly:
 *   1. Centralises the conversion between UI types and core types (the UI
 *      may want a label string for the mode dropdown; the core only knows
 *      the enum value).
 *   2. Lets us swap the core target without touching every UI control.
 *   3. Provides a single place to add automation/undo hooks later.
 *
 * Threading: UI thread only. The core's atomic setters/getters handle the
 * audio-thread crossing internally.
 */
class MasterStripCeilingBinding
{
public:
    MasterStripCeilingBinding() = default;

    void setCeiling(MasterCeilingCore* c) noexcept { ceiling_ = c; }
    bool isBound() const noexcept                  { return ceiling_ != nullptr; }

    // ── Mode ─────────────────────────────────────────────────────────

    using Mode = MasterCeilingCore::Mode;

    void setMode(Mode m)
    {
        if (ceiling_ != nullptr)
            ceiling_->setMode(m);
    }

    Mode getMode() const
    {
        return ceiling_ != nullptr ? ceiling_->getMode() : Mode::Off;
    }

    /** Human-readable label for a given mode — used by dropdowns/readouts. */
    static juce::String labelForMode(Mode m)
    {
        switch (m)
        {
            case Mode::Off:               return "Off";
            case Mode::HardClip:          return "Hard Clip";
            case Mode::SoftClip:          return "Soft Clip";
            case Mode::LookaheadLimiter:  return "Lookahead";
        }
        return "Off";
    }

    /** Iteration helper for dropdown population. */
    static juce::StringArray getAllModeLabels()
    {
        return { "Off", "Hard Clip", "Soft Clip", "Lookahead" };
    }

    static Mode modeFromIndex(int index)
    {
        switch (index)
        {
            case 0:  return Mode::Off;
            case 1:  return Mode::HardClip;
            case 2:  return Mode::SoftClip;
            case 3:  return Mode::LookaheadLimiter;
            default: return Mode::Off;
        }
    }

    static int indexFromMode(Mode m)
    {
        switch (m)
        {
            case Mode::Off:               return 0;
            case Mode::HardClip:          return 1;
            case Mode::SoftClip:          return 2;
            case Mode::LookaheadLimiter:  return 3;
        }
        return 0;
    }

    // ── Ceiling dB ───────────────────────────────────────────────────

    void setCeilingDb(float db)
    {
        if (ceiling_ != nullptr)
            ceiling_->setCeilingDb(db);
    }

    float getCeilingDb() const
    {
        return ceiling_ != nullptr ? ceiling_->getCeilingDb() : -0.1f;
    }

    static constexpr float kMinCeilingDb = -24.0f;
    static constexpr float kMaxCeilingDb =   0.0f;

    // ── Gain reduction (read-only metering) ──────────────────────────

    /**
     * Returns the current gain reduction in dB (always <= 0).
     * 0.0 means no reduction; -3.0 means 3 dB of reduction.
     * Only meaningful when mode is LookaheadLimiter; other modes return 0.
     */
    float getGainReductionDb() const
    {
        return ceiling_ != nullptr ? ceiling_->getGainReductionDb() : 0.0f;
    }

private:
    MasterCeilingCore* ceiling_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripCeilingBinding)
};

} // namespace DAW
