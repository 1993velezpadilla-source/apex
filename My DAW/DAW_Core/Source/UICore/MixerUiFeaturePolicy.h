#pragma once

namespace DAW {

/** UI-only product policy for Mixer features.
    This does not remove or disable the corresponding backend/core state. */
struct MixerUiFeaturePolicy final
{
    static constexpr bool kMasterUtilityUserControlEnabled = false;
};

} // namespace DAW
