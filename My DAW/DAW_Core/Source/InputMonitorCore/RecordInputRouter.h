#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * RecordInputRouter — routes hardware input to record and/or monitor paths.
 *
 * Simplified to only support dry recording (no plugin effects printed).
 * Wet/printed recording modes were removed — no major DAW offers them
 * as a per-track toggle, and they introduced zipper noise from redundant
 * signal paths.
 */
class RecordInputRouter
{
public:
    enum class Mode
    {
        MonitorDryRecordDry
    };

    void setMode(Mode) noexcept {}
    Mode getMode() const noexcept { return Mode::MonitorDryRecordDry; }

    static juce::String getModeLabel(Mode)
    {
        return "Dry";
    }

private:
    Mode mode_ = Mode::MonitorDryRecordDry;
};

} // namespace DAW
