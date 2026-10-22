#pragma once

namespace DAW
{
// APEX is audio-first for this release: recording, editing, takes/comping,
// mixing, plugins and automation. Keep the existing Piano Roll implementation
// in source control, but never expose or activate its launch controls now.
//
// A deliberate future beat-making release can switch this single gate back
// on and run its own interaction, audio-engine and device validation.
struct BeatMakingFeaturePolicy final
{
    static constexpr bool kPianoRollEnabled = false;
};
} // namespace DAW
