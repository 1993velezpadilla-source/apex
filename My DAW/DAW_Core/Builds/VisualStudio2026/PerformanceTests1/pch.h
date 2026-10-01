// pch.h: Precompiled header + minimal JUCE shim for standalone perf tests.
// Provides only the JUCE primitives consumed by TruePeakMeterCore and
// SamplePeakMeterCore so the test project needs no JUCE compilation unit.

#ifndef PCH_H
#define PCH_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Minimal juce:: shim — covers every symbol used by TruePeakMeterCore.h
// and SamplePeakMeterCore.h without pulling in the full JUCE build.
// ---------------------------------------------------------------------------
#define JUCE_HEADER_H_INCLUDED  // prevent JuceHeader.h re-inclusion after shim

namespace juce
{
    template<typename T> inline T jmax(T a, T b)               { return a > b ? a : b; }
    template<typename T> inline T jmax(T a, T b, T c)          { return jmax(a, jmax(b, c)); }
    template<typename T> inline T jmin(T a, T b)               { return a < b ? a : b; }
    template<typename T> inline T jmin(T a, T b, T c)          { return jmin(a, jmin(b, c)); }
    template<typename T> inline T jlimit(T lo, T hi, T v)      { return jmax(lo, jmin(hi, v)); }
    inline void ignoreUnused(...) {}

    template<typename T>
    struct MathConstants
    {
        static constexpr T pi   = T(3.14159265358979323846);
        static constexpr T twoPi = T(6.28318530717958647692);
    };

    struct Decibels
    {
        static float decibelsToGain(float dB, float minusInfinity = -100.f)
        {
            return dB > minusInfinity
                ? std::pow(10.f, dB * 0.05f)
                : 0.f;
        }
        static float gainToDecibels(float gain, float minusInfinity = -100.f)
        {
            return gain > 0.f
                ? jmax(minusInfinity, std::log10(gain) * 20.f)
                : minusInfinity;
        }
    };
} // namespace juce

// Guard so the real JuceHeader.h is never included from within the test build.
#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1

// Now JuceHeader.h will be satisfied by the guard below instead of the real file.
// The test cpp includes TruePeakMeterCore.h which does #include <JuceHeader.h>;
// we satisfy that with a dummy header trick via the include-path shim header.

#endif // PCH_H
