#pragma once

#include <array>
#include <cstddef>

namespace APEX::TuneLive {

enum class VocalRange { Automatic, Low, Medium, High };

struct VocalRangeInfo {
    const char* label;
    float minimumHz;
    float maximumHz;
};

inline constexpr std::array<VocalRangeInfo, 4> kVocalRanges{{
    {"Automatic", 55.0f, 1200.0f},
    {"Low",       55.0f,  350.0f},
    {"Medium",    85.0f,  700.0f},
    {"High",     155.0f, 1200.0f}
}};

inline const VocalRangeInfo& rangeInfo(VocalRange range) noexcept {
    const auto index = static_cast<std::size_t>(range);
    return kVocalRanges[index < kVocalRanges.size() ? index : 0];
}

struct DspSettings {
    float correction=0.85f;        // 0..1
    float referenceHz=440.0f;      // 430..450 (calibrated A4)
    float retune=0.20f;            // 0 = 1 ms, 1 = 150 ms
    int key=0;                     // C=0, C#=1... B=11
    int scale=0;                   // Chromatic=0, Major=1, NaturalMinor=2
    float wet=1.0f;               // 0..1
    bool bypass=false;
    float humanize=0.70f;         // 0..1; preserve small natural inflections
    float flexTune=0.35f;         // 0..1; musical micro-pitch dead-zone
    float stability=0.40f;        // 0..1 note-target hysteresis
    bool preserveFormants=true;
    VocalRange vocalRange=VocalRange::Automatic;
    float vibratoPreserve=0.0f;  // Preserve detected periodic modulation.
};

} // namespace APEX::TuneLive
