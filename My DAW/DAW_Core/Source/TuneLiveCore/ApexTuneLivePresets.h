#pragma once
// APEX Tune Live production-facing artist workflows.
// Presets are control configurations, not private Antares algorithms.
// The existing hosted parameter indices/IDs are immutable.
#include <array>
#include <cstddef>

namespace APEX { namespace TuneLive {

enum class VocalStyle { Natural = 0, Modern = 1, HardTune = 2 };

struct StylePreset {
    const char* name;
    const char* explanation;
    float amount;
    float retune;     // normalised (0..1), maps to 1..150ms
    float humanize;
    float flexTune;
    float stability;
    float mix;
    bool preserveFormants;
};

inline constexpr std::array<StylePreset, 3> kVocalStyles{{
    // Keep a singer's own vibrato, micro-inflections and voice identity.
    {"NATURAL", "Soft correction; keep the singer's expression", 0.85f, 0.22f,
     0.85f, 0.50f, 0.65f, 1.0f, true},
    // Contemporary bright vocal: clearer note centres with smooth transitions.
    {"MODERN", "Tight modern vocal without flatlined expression", 0.96f, 0.065f,
     0.48f, 0.12f, 0.65f, 1.0f, true},
    // Explicit stylistic robotic effect, not the factory default.
    {"HARD TUNE", "Deliberately audible hard pitch correction", 1.0f, 0.0f,
     0.0f, 0.0f, 0.24f, 1.0f, true}
}};

inline constexpr const StylePreset& preset(VocalStyle style) noexcept {
    const auto index = static_cast<unsigned int>(style);
    return kVocalStyles[index < kVocalStyles.size() ? index : 0];
}

}} // APEX::TuneLive
