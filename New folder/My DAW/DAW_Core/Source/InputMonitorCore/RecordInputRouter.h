#pragma once
#include <JuceHeader.h>
#include "InputFxChain.h"

namespace DAW {

/**
 * RecordInputRouter — routes hardware input to record and/or monitor paths.
 *
 * Supports three recording/monitoring modes:
 *   A. MonitorDryRecordDry   — simplest; no FX on input at all
 *   B. MonitorWetRecordDry   — performer hears InputFxChain; recorded file is dry
 *   C. MonitorWetRecordWet   — performer hears InputFxChain; recorded file is printed with FX
 *
 * IMPORTANT DISTINCTIONS:
 *   - "Printed FX" = InputFxChain applied to the RECORD path (intentional)
 *   - "Monitor-only FX" = InputFxChain applied to MONITOR path only
 *   - "Control Room FX" = MonitorFxChain in ControlRoomEngine (always monitor-only)
 *
 * The user must INTENTIONALLY choose to print the plugin chain.
 * Wet monitoring does NOT automatically mean wet recording.
 */
class RecordInputRouter
{
public:
    enum class Mode
    {
        MonitorDryRecordDry,   // no FX anywhere
        MonitorWetRecordDry,   // FX on monitor only; record file stays dry
        MonitorWetRecordWet    // FX on both paths; record file is printed
    };

    void setMode(Mode m) noexcept { mode_ = m; }
    Mode getMode() const noexcept { return mode_; }

    /**
     * Routes input samples to monitor and record buffers.
     *
     * @param inputL/R        Raw hardware input (dry)
     * @param monitorOutL/R   What the performer hears in real time
     * @param recordOutL/R    What gets written to the recorded file
     * @param numSamples      Block size
     * @param inputFx         Per-track InputFxChain (may be null or bypassed)
     */
    void routeInput(const float* inputL, const float* inputR,
                    float* monitorOutL, float* monitorOutR,
                    float* recordOutL, float* recordOutR,
                    int numSamples,
                    InputFxChain* inputFx = nullptr) noexcept
    {
        if (!inputL || !inputR) return;

        switch (mode_)
        {
            case Mode::MonitorDryRecordDry:
            {
                // Both paths get the raw dry input — no FX anywhere
                for (int i = 0; i < numSamples; ++i)
                {
                    if (monitorOutL) monitorOutL[i] = inputL[i];
                    if (monitorOutR) monitorOutR[i] = inputR[i];
                    if (recordOutL)  recordOutL[i]  = inputL[i];
                    if (recordOutR)  recordOutR[i]  = inputR[i];
                }
                break;
            }

            case Mode::MonitorWetRecordDry:
            {
                // Record path: dry input
                for (int i = 0; i < numSamples; ++i)
                {
                    if (recordOutL) recordOutL[i] = inputL[i];
                    if (recordOutR) recordOutR[i] = inputR[i];
                }
                // Monitor path: copy dry → apply InputFxChain
                if (monitorOutL && monitorOutR)
                {
                    for (int i = 0; i < numSamples; ++i)
                    {
                        monitorOutL[i] = inputL[i];
                        monitorOutR[i] = inputR[i];
                    }
                    if (inputFx && !inputFx->isBypassed())
                        inputFx->process(monitorOutL, monitorOutR, numSamples);
                }
                break;
            }

            case Mode::MonitorWetRecordWet:
            {
                // Both paths get the wet (printed) signal
                // Process into monitor buffer, then copy to record buffer
                if (monitorOutL && monitorOutR)
                {
                    for (int i = 0; i < numSamples; ++i)
                    {
                        monitorOutL[i] = inputL[i];
                        monitorOutR[i] = inputR[i];
                    }
                    if (inputFx && !inputFx->isBypassed())
                        inputFx->process(monitorOutL, monitorOutR, numSamples);
                    // Record gets the same wet signal
                    if (recordOutL && recordOutR)
                    {
                        for (int i = 0; i < numSamples; ++i)
                        {
                            recordOutL[i] = monitorOutL[i];
                            recordOutR[i] = monitorOutR[i];
                        }
                    }
                }
                else
                {
                    // Fallback: if no monitor buffer, process directly into record
                    for (int i = 0; i < numSamples; ++i)
                    {
                        if (recordOutL) recordOutL[i] = inputL[i];
                        if (recordOutR) recordOutR[i] = inputR[i];
                    }
                    if (inputFx && !inputFx->isBypassed() && recordOutL && recordOutR)
                        inputFx->process(recordOutL, recordOutR, numSamples);
                }
                break;
            }
        }
    }

    /** Returns a human-readable label for the current mode. */
    static juce::String getModeLabel(Mode m)
    {
        switch (m)
        {
            case Mode::MonitorDryRecordDry:  return "Dry / Dry";
            case Mode::MonitorWetRecordDry:  return "Wet Mon / Dry Rec";
            case Mode::MonitorWetRecordWet:  return "Wet Mon / Printed Rec";
            default: return "?";
        }
    }

private:
    Mode mode_ = Mode::MonitorDryRecordDry;
};

} // namespace DAW
