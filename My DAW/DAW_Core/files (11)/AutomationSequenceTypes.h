// ============================================================
// AutomationSequenceTypes.h
// APEX — Create Sequence system  |  Source/AutomationSequence/
//
// Shared data structures and enums.
// No logic, no JUCE UI references — POD only.
// ============================================================
#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <functional>
#include <cstdint>

namespace APEX {
namespace AutomationSeq {

// ---- Constants ------------------------------------------------

static constexpr int    kMaxSteps        = 16;
static constexpr int    kPreviewSamples  = 512;   // horizontal resolution of the live preview

// ---- Shape modes ----------------------------------------------
// Controls the per-step envelope shape.
// "Normal" = full ADSR with all four knobs active.
// All other modes are simplified shapes that ignore some knobs.

enum class ShapeMode : int {
    Normal    = 0,   // ADSR — full envelope control
    Saw       = 1,   // rising linear ramp  (attack only)
    RevSaw    = 2,   // falling linear ramp (release only)
    Triangle  = 3,   // rise then fall
    Gate      = 4,   // square pulse — instant on/off  (Gate length only)
    Sine      = 5,   // smooth arch (sinusoidal within gate)
    Bounce    = 6    // three decaying bounces
};

// ---- Bar view tabs -------------------------------------------
// Selects which per-step scalar is shown in the editable bar view.

enum class BarViewMode : int {
    AttackLevel  = 0,
    DecaySlope   = 1,
    SustainLevel = 2,
    ReleaseSlope = 3
};

// ---- Per-step data -------------------------------------------
// Each of the 16 steps holds its toggle state plus optional
// per-step overrides for the ADSR sliders.
// A negative value means "inherit from global knob."

struct StepData {
    bool  active      = true;
    float probability = 1.0f;   // 0..1  (used by Humanize mode)

    float localAttackLevel  = -1.0f;   // override or -1 = use global
    float localDecaySlope   = -1.0f;
    float localSustainLevel = -1.0f;
    float localReleaseSlope = -1.0f;
};

// ---- Global parameters (all knob values 0..1 normalised) ------

struct SequenceParams {
    float attackLevel   = 1.0f;
    float attackSlope   = 0.25f;   // curve exponent  0 = fast/convex, 1 = linear
    float decaySlope    = 0.35f;
    float sustainLevel  = 0.55f;
    float releaseSlope  = 0.35f;
    float gate          = 0.75f;   // fraction of step duration with envelope active
    float swing         = 0.0f;   // 0 = straight, 1 = maximum push
    float timeMul       = 0.5f;   // 0→0.25x  0.5→1x  1→4x  (log scale)
    float humanize      = 0.0f;   // 0 = exact, 1 = fully randomised

    int       numSteps = 16;
    ShapeMode mode     = ShapeMode::Normal;
    uint32_t  seed     = 12345u;   // reproducible humanize/randomize seed

    std::array<StepData, kMaxSteps> steps = {};
};

// ---- Generated output ----------------------------------------

struct GeneratedCurve {
    std::array<float, kPreviewSamples> samples = {};   // normalised 0..1
    int   numActiveSteps    = 0;
    float timeMulResolved   = 1.0f;   // actual multiplier derived from timeMul
};

} // namespace AutomationSeq
} // namespace APEX
