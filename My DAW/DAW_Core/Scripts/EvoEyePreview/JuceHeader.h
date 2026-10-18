#pragma once
// ONLY a standalone preview build shim. Production uses real JUCE 8.
// Compile exact Source/SplashScreen.h without building the whole DAW.
#define JUCE_WINDOWS 1
#include <algorithm>
#include <cmath>
namespace juce {
template <typename T> inline T jmin(T a, T b) { return std::min(a,b); }
template <typename T> inline T jmax(T a, T b) { return std::max(a,b); }
template <typename T> inline T jlimit(T low, T high, T v) { return std::clamp(v,low,high); }
inline int roundToInt(float v) { return static_cast<int>(std::lround(v)); }
}