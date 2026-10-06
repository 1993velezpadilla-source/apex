#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MonitorOverrideCore — manages which signal feeds the monitor output.
 *
 * Monitor priority order:
 *   1. PFL (highest priority)
 *   2. AFL
 *   3. Solo mix
 *   4. Normal master playback (default)
 *
 * Only the monitor path is affected — master/render paths are never touched.
 */
class MonitorOverrideCore
{
public:
    enum class OverrideSource { None, PFL, AFL, Solo, Custom };

    void setOverride(OverrideSource source) noexcept { override_ = source; }
    OverrideSource getOverride() const noexcept { return override_; }

    bool isOverrideActive() const noexcept { return override_ != OverrideSource::None; }

    /** Clear any active override — monitor returns to normal master playback. */
    void clearOverride() noexcept { override_ = OverrideSource::None; }

private:
    OverrideSource override_ = OverrideSource::None;
};

} // namespace DAW
