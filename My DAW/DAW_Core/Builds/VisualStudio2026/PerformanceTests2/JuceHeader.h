// Minimal JUCE stub for standalone performance tests.
// Provides only what TruePeakMeterCore / SamplePeakMeterCore need.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace juce {

    template <typename T> inline T jmax(T a, T b) noexcept { return a > b ? a : b; }
    template <typename T> inline T jmax(T a, T b, T c) noexcept { return jmax(a, jmax(b, c)); }
    template <typename T> inline T jmin(T a, T b) noexcept { return a < b ? a : b; }
    template <typename T> inline T jmin(T a, T b, T c) noexcept { return jmin(a, jmin(b, c)); }
    template <typename T> inline T jlimit(T lo, T hi, T v) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    struct Decibels {
        static float decibelsToGain(float dB, float minusInfDB = -100.0f) noexcept {
            return dB > minusInfDB ? std::pow(10.0f, dB * 0.05f) : 0.0f;
        }
        static float gainToDecibels(float gain, float minusInfDB = -100.0f) noexcept {
            return gain > 0.0f ? jmax(minusInfDB, 20.0f * std::log10(gain)) : minusInfDB;
        }
    };

} // namespace juce
