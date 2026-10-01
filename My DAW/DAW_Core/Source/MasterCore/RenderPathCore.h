#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * RenderPathCore — render/export signal path.
 *
 * Taps the master bus output AFTER master inserts and master fader,
 * but BEFORE the monitor section.
 *
 * Render path MUST respect:
 *   - Master FX chain
 *   - Master fader
 *   - Master mute
 *   - All routing before master
 *
 * Render path MUST ignore:
 *   - Monitor volume knob
 *   - Monitor dim
 *   - Monitor mute
 *   - Speaker selection
 *   - Monitor FX
 *
 * This ensures exported files match the mix, not the monitoring.
 */
class RenderPathCore
{
public:
    void setExporting(bool exporting) noexcept { exporting_ = exporting; }
    bool isExporting() const noexcept { return exporting_; }

    /** Returns true if the render path should capture audio. */
    bool shouldCapture() const noexcept { return exporting_; }

private:
    bool exporting_ = false;
};

} // namespace DAW
