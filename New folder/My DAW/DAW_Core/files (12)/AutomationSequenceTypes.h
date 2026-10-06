// ============================================================
// AutomationSequenceTypes.h  — v3 (Playground)
// APEX · Create Sequence  |  Source/AutomationSequence/
//
// 70 shape modes across 12 categories.
// Shared POD only — no logic, no JUCE UI.
// ============================================================
#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cstdint>

namespace APEX {
namespace AutomationSeq {

// ---- Constants -----------------------------------------------
static constexpr int kMaxSteps       = 16;
static constexpr int kPreviewSamples = 512;
static constexpr int kMaxShapesPerCategory = 8;

// ---- Categories ----------------------------------------------
enum class ShapeCategory : int {
    Envelope = 0,   // ADSR family — triggered envelopes
    Ramp,           // Linear ramps and slopes
    Wave,           // Periodic oscillators
    Physics,        // Simulated physical phenomena
    Organic,        // Natural / biological feel
    Curve,          // Mathematical transfer curves
    Pulse,          // Gate / duty-cycle patterns
    Mod,            // Modulation patterns (AM/FM/PWM)
    Random,         // Stochastic processes
    Glitch,         // Digital artifacts
    Algorithm,      // Discrete algorithmic sequences
    Exotic,         // Strange attractors and fractals
    kCount = 12
};

// ---- 70 shape modes ------------------------------------------
enum class ShapeMode : int {
    // ── ENVELOPE (0-6) ───────────────────────────────────────
    Normal      = 0,    // Full ADSR
    AD          = 1,    // Attack + Decay (no sustain)
    AR          = 2,    // Attack + Release
    AHR         = 3,    // Attack + Hold + Release
    AHDSR       = 4,    // Attack + Hold + Decay + Sustain + Release
    Gate        = 5,    // Square gate pulse
    GateInv     = 6,    // Inverted gate (high then drop)

    // ── RAMP (7-12) ──────────────────────────────────────────
    Saw         = 7,    // Linear rise
    RevSaw      = 8,    // Linear fall
    Triangle    = 9,    // Symmetric rise + fall
    StairUp     = 10,   // 6-step ascending staircase
    StairDn     = 11,   // 6-step descending staircase
    ZigZag      = 12,   // Alternating up/down saws

    // ── WAVE (13-19) ─────────────────────────────────────────
    Sine        = 13,   // Half-sine arch
    SineDbl     = 14,   // Two arches per step
    SineSq      = 15,   // sin²(x)
    SineCube    = 16,   // sin³(x) — sharper peaks
    Tremolo     = 17,   // 8 fast sine cycles
    Flutter     = 18,   // Multi-irrational sine sum
    WaveFold    = 19,   // Wave-folded sine (audio synth style)

    // ── PHYSICS (20-25) ──────────────────────────────────────
    Bounce      = 20,   // 3 decaying Gaussian bounces
    Spring      = 21,   // Damped overshoot oscillation
    Pendulum    = 22,   // Cosine swing
    Heartbeat   = 23,   // P-QRS-T cardiac shape
    Drumroll    = 24,   // Accelerating hits
    Pluck       = 25,   // Karplus-Strong style (noise burst + decay)

    // ── ORGANIC (26-31) ──────────────────────────────────────
    Breathe     = 26,   // Sigmoid inhale + smooth exhale
    Wobble      = 27,   // FM-modulated sine
    Stutter     = 28,   // Rapid gate trill (8 pulses)
    Ratchet     = 29,   // 4 ascending mini-ramps
    Vinyl       = 30,   // Sparse crackle hits
    Telegraph   = 31,   // Morse-like dot/dash pattern

    // ── CURVE (32-37) ────────────────────────────────────────
    ExpIn       = 32,   // Accelerating ^3
    ExpOut      = 33,   // Decelerating 1-(1-t)^3
    SCurve      = 34,   // Smoothstep ease in/out
    LogCurve    = 35,   // Square-root / logarithmic
    Convex      = 36,   // ^0.5 (slow at peak)
    Concave     = 37,   // ^2 (fast at peak)

    // ── PULSE (38-42) ────────────────────────────────────────
    NarrowPulse = 38,   // 15% duty pulse
    WidePulse   = 39,   // 80% duty pulse
    DoublePulse = 40,   // Two pulses per step
    TriplePulse = 41,   // Three pulses per step
    MorseRand   = 42,   // Seeded random Morse pattern

    // ── MOD (43-47) ──────────────────────────────────────────
    AM          = 43,   // Amplitude modulation
    FM          = 44,   // Frequency modulation
    RingMod     = 45,   // Ring modulation
    PWM         = 46,   // Pulse width modulation
    Comb        = 47,   // Comb-filter peaks pattern

    // ── RANDOM (48-53) ───────────────────────────────────────
    WhiteNs     = 48,   // Per-sample seeded noise
    Brownian    = 49,   // Random walk (smooth)
    SmthRnd     = 50,   // Cubic-interpolated random keys
    DrunkWalk   = 51,   // Bounded random walk
    RndGate     = 52,   // Random gate length per step
    LFSR        = 53,   // 8-bit Galois LFSR pseudo-random

    // ── GLITCH (54-59) ───────────────────────────────────────
    BitCrsh     = 54,   // 8-level quantised sine
    ChaosMp     = 55,   // Logistic map (r≈3.95)
    Euclid      = 56,   // Bjorklund active-step distribution
    SampHold    = 57,   // S&H — random held values
    BitShift    = 58,   // Bit-shifted ascending values
    RobotStep   = 59,   // Quantised hard transitions

    // ── ALGORITHM (60-64) ────────────────────────────────────
    Rule30      = 60,   // Wolfram cellular automaton Rule 30
    Rule110     = 61,   // Wolfram CA Rule 110
    PrimeSieve  = 62,   // Active where index is prime
    Fibonacci   = 63,   // Fibonacci ratios mod 1
    Markov      = 64,   // 4-state Markov chain walk

    // ── EXOTIC (65-69) ───────────────────────────────────────
    Lorenz      = 65,   // Lorenz attractor X-projection
    Henon       = 66,   // Henon map
    Mandelbrot  = 67,   // Mandelbrot escape-time
    Cantor      = 68,   // Cantor set membership
    Sierpinski  = 69,   // Sierpinski triangle membership

    kCount      = 70
};

// ---- Category metadata table ---------------------------------
struct CategoryInfo {
    ShapeCategory category;
    const char*   label;
    ShapeMode     modes[kMaxShapesPerCategory];
    int           modeCount;
};

inline constexpr CategoryInfo kCategoryInfo[12] = {
    { ShapeCategory::Envelope,  "ENV",
      { ShapeMode::Normal,   ShapeMode::AD,        ShapeMode::AR,
        ShapeMode::AHR,      ShapeMode::AHDSR,     ShapeMode::Gate,
        ShapeMode::GateInv }, 7 },

    { ShapeCategory::Ramp,      "RAMP",
      { ShapeMode::Saw,      ShapeMode::RevSaw,    ShapeMode::Triangle,
        ShapeMode::StairUp,  ShapeMode::StairDn,   ShapeMode::ZigZag }, 6 },

    { ShapeCategory::Wave,      "WAVE",
      { ShapeMode::Sine,     ShapeMode::SineDbl,   ShapeMode::SineSq,
        ShapeMode::SineCube, ShapeMode::Tremolo,   ShapeMode::Flutter,
        ShapeMode::WaveFold }, 7 },

    { ShapeCategory::Physics,   "PHYS",
      { ShapeMode::Bounce,   ShapeMode::Spring,    ShapeMode::Pendulum,
        ShapeMode::Heartbeat,ShapeMode::Drumroll,  ShapeMode::Pluck }, 6 },

    { ShapeCategory::Organic,   "ORG",
      { ShapeMode::Breathe,  ShapeMode::Wobble,    ShapeMode::Stutter,
        ShapeMode::Ratchet,  ShapeMode::Vinyl,     ShapeMode::Telegraph }, 6 },

    { ShapeCategory::Curve,     "CURVE",
      { ShapeMode::ExpIn,    ShapeMode::ExpOut,    ShapeMode::SCurve,
        ShapeMode::LogCurve, ShapeMode::Convex,    ShapeMode::Concave }, 6 },

    { ShapeCategory::Pulse,     "PULSE",
      { ShapeMode::NarrowPulse, ShapeMode::WidePulse, ShapeMode::DoublePulse,
        ShapeMode::TriplePulse, ShapeMode::MorseRand }, 5 },

    { ShapeCategory::Mod,       "MOD",
      { ShapeMode::AM,       ShapeMode::FM,        ShapeMode::RingMod,
        ShapeMode::PWM,      ShapeMode::Comb }, 5 },

    { ShapeCategory::Random,    "RNG",
      { ShapeMode::WhiteNs,  ShapeMode::Brownian,  ShapeMode::SmthRnd,
        ShapeMode::DrunkWalk,ShapeMode::RndGate,   ShapeMode::LFSR }, 6 },

    { ShapeCategory::Glitch,    "GLITCH",
      { ShapeMode::BitCrsh,  ShapeMode::ChaosMp,   ShapeMode::Euclid,
        ShapeMode::SampHold, ShapeMode::BitShift,  ShapeMode::RobotStep }, 6 },

    { ShapeCategory::Algorithm, "ALGO",
      { ShapeMode::Rule30,   ShapeMode::Rule110,   ShapeMode::PrimeSieve,
        ShapeMode::Fibonacci,ShapeMode::Markov }, 5 },

    { ShapeCategory::Exotic,    "EXOT",
      { ShapeMode::Lorenz,   ShapeMode::Henon,     ShapeMode::Mandelbrot,
        ShapeMode::Cantor,   ShapeMode::Sierpinski }, 5 },
};

// ---- Shape label lookup --------------------------------------
inline const char* getShapeLabel(ShapeMode m) noexcept {
    switch (m) {
        case ShapeMode::Normal:      return "ADSR";
        case ShapeMode::AD:          return "A+D";
        case ShapeMode::AR:          return "A+R";
        case ShapeMode::AHR:         return "AHR";
        case ShapeMode::AHDSR:       return "AHDSR";
        case ShapeMode::Gate:        return "Gate";
        case ShapeMode::GateInv:     return "GateInv";
        case ShapeMode::Saw:         return "Saw";
        case ShapeMode::RevSaw:      return "RevSaw";
        case ShapeMode::Triangle:    return "Tri";
        case ShapeMode::StairUp:     return "Stair+";
        case ShapeMode::StairDn:     return "Stair-";
        case ShapeMode::ZigZag:      return "ZigZag";
        case ShapeMode::Sine:        return "Sine";
        case ShapeMode::SineDbl:     return "Sin x2";
        case ShapeMode::SineSq:      return "Sin^2";
        case ShapeMode::SineCube:    return "Sin^3";
        case ShapeMode::Tremolo:     return "Trem";
        case ShapeMode::Flutter:     return "Flut";
        case ShapeMode::WaveFold:    return "Fold";
        case ShapeMode::Bounce:      return "Bounce";
        case ShapeMode::Spring:      return "Spring";
        case ShapeMode::Pendulum:    return "Pend";
        case ShapeMode::Heartbeat:   return "Heart";
        case ShapeMode::Drumroll:    return "Roll";
        case ShapeMode::Pluck:       return "Pluck";
        case ShapeMode::Breathe:     return "Breath";
        case ShapeMode::Wobble:      return "Wobble";
        case ShapeMode::Stutter:     return "Stutter";
        case ShapeMode::Ratchet:     return "Ratch";
        case ShapeMode::Vinyl:       return "Vinyl";
        case ShapeMode::Telegraph:   return "Tele";
        case ShapeMode::ExpIn:       return "Exp+";
        case ShapeMode::ExpOut:      return "Exp-";
        case ShapeMode::SCurve:      return "S-Cur";
        case ShapeMode::LogCurve:    return "Log";
        case ShapeMode::Convex:      return "Cnvx";
        case ShapeMode::Concave:     return "Ccav";
        case ShapeMode::NarrowPulse: return "Narrw";
        case ShapeMode::WidePulse:   return "Wide";
        case ShapeMode::DoublePulse: return "Dbl";
        case ShapeMode::TriplePulse: return "Trpl";
        case ShapeMode::MorseRand:   return "Morse";
        case ShapeMode::AM:          return "AM";
        case ShapeMode::FM:          return "FM";
        case ShapeMode::RingMod:     return "Ring";
        case ShapeMode::PWM:         return "PWM";
        case ShapeMode::Comb:        return "Comb";
        case ShapeMode::WhiteNs:     return "White";
        case ShapeMode::Brownian:    return "Brown";
        case ShapeMode::SmthRnd:     return "Smth";
        case ShapeMode::DrunkWalk:   return "Drunk";
        case ShapeMode::RndGate:     return "RndGt";
        case ShapeMode::LFSR:        return "LFSR";
        case ShapeMode::BitCrsh:     return "Bits";
        case ShapeMode::ChaosMp:     return "Chaos";
        case ShapeMode::Euclid:      return "Eucld";
        case ShapeMode::SampHold:    return "S&H";
        case ShapeMode::BitShift:    return "Shift";
        case ShapeMode::RobotStep:   return "Robot";
        case ShapeMode::Rule30:      return "R30";
        case ShapeMode::Rule110:     return "R110";
        case ShapeMode::PrimeSieve:  return "Prime";
        case ShapeMode::Fibonacci:   return "Fib";
        case ShapeMode::Markov:      return "Mrkov";
        case ShapeMode::Lorenz:      return "Lornz";
        case ShapeMode::Henon:       return "Henon";
        case ShapeMode::Mandelbrot:  return "Mandl";
        case ShapeMode::Cantor:      return "Cantr";
        case ShapeMode::Sierpinski:  return "Sierp";
        default:                     return "?";
    }
}

inline ShapeCategory getCategoryForMode(ShapeMode m) noexcept {
    const int idx = static_cast<int>(m);
    if (idx <= 6)  return ShapeCategory::Envelope;
    if (idx <= 12) return ShapeCategory::Ramp;
    if (idx <= 19) return ShapeCategory::Wave;
    if (idx <= 25) return ShapeCategory::Physics;
    if (idx <= 31) return ShapeCategory::Organic;
    if (idx <= 37) return ShapeCategory::Curve;
    if (idx <= 42) return ShapeCategory::Pulse;
    if (idx <= 47) return ShapeCategory::Mod;
    if (idx <= 53) return ShapeCategory::Random;
    if (idx <= 59) return ShapeCategory::Glitch;
    if (idx <= 64) return ShapeCategory::Algorithm;
    return ShapeCategory::Exotic;
}

// ---- Bar view tabs -------------------------------------------
enum class BarViewMode : int {
    AttackLevel  = 0,
    DecaySlope   = 1,
    SustainLevel = 2,
    ReleaseSlope = 3
};

// ---- Randomize flavors ---------------------------------------
enum class RandomFlavor : int {
    Everything = 0,   // Full randomization
    ShapeOnly,        // Just pick a new shape mode
    StepsOnly,        // Just shuffle active steps
    KnobsOnly,        // Just randomize ADSR / gate / swing
    Mutate,           // Small variation from current
    Wild,             // Extreme settings
    Musical,          // Curated good-sounding settings
    kCount
};

// ---- Per-step data -------------------------------------------
struct StepData {
    bool  active      = true;
    float probability = 1.0f;
    float localAttackLevel  = -1.0f;
    float localDecaySlope   = -1.0f;
    float localSustainLevel = -1.0f;
    float localReleaseSlope = -1.0f;
};

// ---- Global parameters ---------------------------------------
struct SequenceParams {
    float attackLevel  = 1.0f;
    float attackSlope  = 0.25f;
    float decaySlope   = 0.35f;
    float sustainLevel = 0.55f;
    float releaseSlope = 0.35f;
    float gate         = 0.75f;
    float swing        = 0.0f;
    float timeMul      = 0.5f;
    float humanize     = 0.0f;
    float chaos        = 0.0f;
    int   repeatCount  = 1;

    int       numSteps = 16;
    ShapeMode mode     = ShapeMode::Normal;
    uint32_t  seed     = 12345u;

    std::array<StepData, kMaxSteps> steps = {};
};

struct GeneratedCurve {
    std::array<float, kPreviewSamples> samples = {};
    int   numActiveSteps  = 0;
    float timeMulResolved = 1.0f;
};

// ---- Saved pattern (for library) -----------------------------
struct SavedPattern {
    juce::String   name;
    SequenceParams params;
    int64_t        savedAtMs = 0;
};

} // namespace AutomationSeq
} // namespace APEX
