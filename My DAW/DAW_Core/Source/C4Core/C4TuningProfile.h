#pragma once
#include <JuceHeader.h>
#include "C4Types.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4TuningProfile — the centralized tuning contract for C4 BLOOM CORE.
//
// EVERY sonic constant that may be varied for controlled listening tests
// lives here. The DSP architecture is frozen; these constants are
// deliberately NOT. Named variants (A/B/C) exist so level-matched listening
// tests can switch entire tuning families without rewriting DSP.
//
// Nothing here is a magic number: every value has a named meaning and a
// measurement intent. Values marked CANDIDATE are Phase-1 engineering
// starting points that must survive (or lose to) the Phase 5 blind tests.
//
// Thread contract: profiles are const data read at prepare()/parameter
// adoption time. The audio thread never mutates them.
// ============================================================================

// ---------------------------------------------------------------------------
// Per-band gain/Q law ("BLOOM GEOMETRY", spec §8/§9).
//
// Effective Q is derived from the user Q and the gain magnitude:
//
//   Q_eff = Q_base * (1 + qDrive * pow (|gainDb| / kGainMaxDb, qLawExponent))
//
//   for BOOST, and the same shape with cutQDrive for CUT. A drive of 0 gives
//   constant Q (Q_eff == Q_base). Positive drive narrows the response as the
//   gain magnitude grows: small boosts stay broad, larger boosts become more
//   focused ("musical proportional Q"), cuts may use a separate drive so
//   they behave more controlled/focused than boosts without ever becoming
//   surgical.
// ---------------------------------------------------------------------------

struct C4BandGeometry
{
    float baseBellQ;      // Q at 0 dB (bell mode)
    float baseShelfQ;     // Q at 0 dB (shelf mode; transition width control)
    float boostQDrive;    // proportional narrowing, boosts
    float cutQDrive;      // proportional narrowing, cuts (asymmetry allowed)
    float qLawExponent;   // shape of the gain->Q law (1.0 linear, <1 aggressive early)
    bool  geometryEnabled;// false = constant Q (BITE default), true = proportional
};

// ---------------------------------------------------------------------------
// Bloom Residual Bank tuning (spec §12–§14). Phase 4 consumes these.
// ---------------------------------------------------------------------------

struct C4BloomColorTuning
{
    float residualLevel;   // max residual mix (at BLOOM=10, full boost)
    float evenWeight;      // 0..1 blend of even (x^2-family) vs odd (x^3-family) core
    float driveRefDb;      // internal operating reference level (calibrated, measured)
    float activationExp;   // boost->excitation exponent (0 = gain-independent)
    float cutSuppression;  // 0..1 how much excitation cuts are suppressed
};

// ---------------------------------------------------------------------------
// Adjacent-band coupling (spec §9/§10): contour continuity between boosted
// neighbors. Architecture: a "contour bell" between facing shoulders; it
// never subtracts from the user's requested peak gain.
// ---------------------------------------------------------------------------

struct C4CouplingTuning
{
    bool  enabled;
    float strength;        // 0..1 coupling amount (variant A = 0.0 = off)
    float overlapThreshold;// minimum normalized overlap to engage
    float contourQ;        // width of the contour bell (lower = wider)
    float maxContourDb;    // cap on the contour contribution at its center
};

// ---------------------------------------------------------------------------
// HALO (spec §3 OPEN / §10): control-frequency mapping into a realizable
// in-band response.
// ---------------------------------------------------------------------------

struct C4HaloTuning
{
    float mappingSoftness; // 0..1: 0 = straight shelf up to Nyquist limit,
                           //       1 = progressively gentler as control rises
    float amount;          // HALO character gain (Phase 2/5)
    float maxRealizableRatio; // control freq / Nyquist clamp ceiling
};

// ---------------------------------------------------------------------------
// Smoothing times (spec §19). Sample-rate consistent by construction: every
// smoother derives its per-sample coefficient from these times.
// ---------------------------------------------------------------------------

struct C4SmoothingTuning
{
    float gainMs  = 15.0f;
    float freqMs  = 20.0f;
    float qMs     = 15.0f;
    float trimMs  = 10.0f;
    float bloomMs = 25.0f;
    float modeMs  = 8.0f;   // enable / Bell-Shelf / HALO transitions
    float bypassMs = 10.0f;
};

// ---------------------------------------------------------------------------
// The complete profile.
// ---------------------------------------------------------------------------

struct C4TuningProfile
{
    const char* name;                       // variant label for tests/reports
    C4BandGeometry geometry[kNumBands];
    C4BloomColorTuning color[kNumBands];
    C4CouplingTuning coupling;
    C4HaloTuning halo;
    C4SmoothingTuning smoothing;
    float bloomDefault;                     // 0..1 normalized (Phase 5 decides)
    float autoGainStrength;                 // 0..1 compensation law strength
    /** BLOOM control law exponent (Phase 4): residual scale =
        bloomNorm^bloomLawExponent. 1.0 = linear in the control; < 1 = more
        color at low settings; > 1 = conservative until high settings.
        Candidate; Phase 5 freezes the subjective mapping. */
    float bloomLawExponent = 1.0f;
    /** Nonlinear-residual oversampling factor (Phase 4 benchmark candidate):
        1 = reference (no oversampling), 2/4 = multirate residual path only.
        The linear EQ is NEVER oversampled. Zero added latency (causal
        interpolation + averaging decimate). */
    int residualOversample = 1;
};

// ============================================================================
// Named variants (historical tuning candidates — NOT frozen; documented
// experiments from Phases 2-4).
//
//   A — conservative reference: constant-Q SCULPT/BITE, no coupling, no color.
//   B — proportional geometry everywhere, subtle coupling, current color guess.
//   C — stronger proportional behavior, medium coupling.
//
// Phase 5 selected the production profile (makeProfileProduction). A/B/C and
// P1 remain as the documented candidate history used by the tuning suites.
// ============================================================================

inline C4TuningProfile makeProfileVariantA()
{
    C4TuningProfile p {};
    p.name = "A";
    p.geometry[(int) C4BandId::Weight] = { 0.75f, 0.60f, 0.0f, 0.0f, 1.0f, true  };
    p.geometry[(int) C4BandId::Sculpt] = { 0.80f, 0.70f, 0.0f, 0.0f, 1.0f, false };
    p.geometry[(int) C4BandId::Bite]   = { 1.00f, 0.80f, 0.0f, 0.0f, 1.0f, false };
    p.geometry[(int) C4BandId::Open]   = { 0.80f, 0.60f, 0.0f, 0.0f, 1.0f, false };

    for (int b = 0; b < kNumBands; ++b)
        p.color[b] = { 0.0f, 0.6f, -12.0f, 1.0f, 1.0f }; // A: no color (reference)

    p.coupling = { false, 0.0f, 0.35f, 1.2f, 0.8f };
    p.halo     = { 0.5f, 1.0f, 0.90f };

    p.bloomDefault      = 0.0f;
    p.autoGainStrength  = 1.0f;
    p.bloomLawExponent  = 1.0f;
    p.residualOversample = 1;
    return p;
}

inline C4TuningProfile makeProfileVariantB()
{
    C4TuningProfile p {};
    p.name = "B";
    p.geometry[(int) C4BandId::Weight] = { 0.75f, 0.60f, 0.60f, 0.75f, 1.0f, true };
    p.geometry[(int) C4BandId::Sculpt] = { 0.80f, 0.70f, 0.90f, 1.10f, 1.0f, true };
    p.geometry[(int) C4BandId::Bite]   = { 1.00f, 0.80f, 0.25f, 0.40f, 1.0f, true };
    p.geometry[(int) C4BandId::Open]   = { 0.80f, 0.60f, 0.35f, 0.35f, 1.0f, true };

    // Phase 4 color candidates (NOT frozen): per-band tonal character —
    // WEIGHT even/H2-heavy and warm; SCULPT even-dominant richness;
    // BITE balanced H2/H3 edge; OPEN lowest amount, sheen only.
    p.color[(int) C4BandId::Weight] = { 0.05f, 0.75f, -12.0f, 1.0f, 1.0f };
    p.color[(int) C4BandId::Sculpt] = { 0.05f, 0.65f, -12.0f, 1.0f, 1.0f };
    p.color[(int) C4BandId::Bite]   = { 0.045f, 0.50f, -12.0f, 1.0f, 1.0f };
    p.color[(int) C4BandId::Open]   = { 0.025f, 0.40f, -12.0f, 1.0f, 1.0f };

    p.coupling = { true, 0.20f, 0.35f, 1.2f, 0.8f };
    p.halo     = { 0.5f, 1.0f, 0.90f };

    p.bloomDefault      = 0.0f;
    p.autoGainStrength  = 1.0f;
    p.bloomLawExponent  = 1.0f;
    p.residualOversample = 2;
    return p;
}

/** The FROZEN Phase 1 reference: identical to variant B (same geometry,
    smoothing, HALO) with BLOOM Coupling OFF. Every Phase 1 regression test
    runs on this profile, so the Phase 1 uncoupled response remains the
    regression baseline while the coupling candidates (A/B/C) are measured
    against it. */
inline C4TuningProfile makeProfilePhase1Reference()
{
    C4TuningProfile p = makeProfileVariantB();
    p.name = "P1";
    p.coupling = { false, 0.20f, 0.35f, 1.2f, 0.8f };
    return p;
}

inline C4TuningProfile makeProfileVariantC()
{
    C4TuningProfile p {};
    p.name = "C";
    p.geometry[(int) C4BandId::Weight] = { 0.75f, 0.60f, 0.90f, 1.10f, 1.0f, true };
    p.geometry[(int) C4BandId::Sculpt] = { 0.80f, 0.70f, 1.40f, 1.60f, 1.0f, true };
    p.geometry[(int) C4BandId::Bite]   = { 1.00f, 0.80f, 0.40f, 0.60f, 1.0f, true };
    p.geometry[(int) C4BandId::Open]   = { 0.80f, 0.60f, 0.50f, 0.50f, 1.0f, true };

    // Phase 4 color candidates (NOT frozen): a stronger candidate with a
    // softer activation exponent and slightly more even character.
    p.color[(int) C4BandId::Weight] = { 0.07f, 0.80f, -12.0f, 0.8f, 1.0f };
    p.color[(int) C4BandId::Sculpt] = { 0.07f, 0.70f, -12.0f, 0.8f, 1.0f };
    p.color[(int) C4BandId::Bite]   = { 0.06f, 0.55f, -12.0f, 0.8f, 1.0f };
    p.color[(int) C4BandId::Open]   = { 0.035f, 0.45f, -12.0f, 0.8f, 1.0f };

    p.coupling = { true, 0.35f, 0.30f, 1.0f, 1.0f };
    p.halo     = { 0.5f, 1.0f, 0.90f };

    p.bloomDefault      = 0.0f;
    p.autoGainStrength  = 1.0f;
    p.bloomLawExponent  = 0.8f;
    p.residualOversample = 4;
    return p;
}

// ============================================================================
// makeProfileProduction — THE FROZEN C4 SOUND (Phase 5, 2026-08-13).
//
// Hybrid design selected by the Phase 5 design-authority process (measurement
// + professional precedent + the C4 North Star; A/B/C remain historical).
// Every value below is FROZEN; the WHY for each value lives in
// docs/C4_PHASE5_PERCEPTUAL_TUNING.md. The profile is const data adopted at
// prepare(); the audio thread never mutates it.
//
// Design intent per band:
//   WEIGHT — broad and substantial boosts (late-proportional law: small
//            boosts stay wide, focus arrives only when pushed); cuts more
//            focused than boosts for controlled cleanup. No bass-gimmick.
//   SCULPT — the "hard to make ugly" band: gently proportional so small
//            moves stay broad and large moves focus progressively; cuts
//            tighter than boosts.
//   BITE — closest to constant-Q of the four (incisive, assertive), with
//          a whisper of proportional behavior so +15 never turns surgical.
//   OPEN  — gentle proportional bell; HALO shelf carries the air identity
//           via the Phase 2 mapping (softness 0.5; amount frozen at unity —
//           the identity is the mapping, never an added-gain trick).
//   Coupling — felt-not-heard contour between adjacent boosts; capped,
//             never subtracts from requested peaks.
//   Color   — per-band residual hierarchy: WEIGHT density/warmth (H2-leaning),
//             SCULPT body/glue, BITE balanced H2/H3 edge, OPEN minimal sheen.
//             Cuts stay clean (cutSuppression 1.0). activation 0.85 so small
//             boosts whisper color early without turning the EQ into a
//             saturator; BLOOM law 0.85 exposes identity at low-mid BLOOM
//             settings; default BLOOM 4.0 (user units, see C4Types.h).
//   Antialias — 2x on the residual path only: measured fold products are
//             inaudible at 1x, but 2x buys top-octave margin (the OPEN-band
//             "glass" risk) at ~2x the residual-path cost with zero latency.
// ============================================================================

inline C4TuningProfile makeProfileProduction()
{
    C4TuningProfile p {};
    p.name = "PROD";

    p.geometry[(int) C4BandId::Weight] = { 0.75f, 0.60f, 0.50f, 0.80f, 1.20f, true };
    p.geometry[(int) C4BandId::Sculpt] = { 0.80f, 0.70f, 1.10f, 1.30f, 0.90f, true };
    p.geometry[(int) C4BandId::Bite]   = { 1.00f, 0.80f, 0.30f, 0.45f, 1.00f, true };
    p.geometry[(int) C4BandId::Open]   = { 0.80f, 0.60f, 0.35f, 0.40f, 1.00f, true };

    p.color[(int) C4BandId::Weight] = { 0.055f, 0.75f, -12.0f, 0.85f, 1.0f };
    p.color[(int) C4BandId::Sculpt] = { 0.055f, 0.65f, -12.0f, 0.85f, 1.0f };
    p.color[(int) C4BandId::Bite]   = { 0.050f, 0.50f, -12.0f, 0.85f, 1.0f };
    p.color[(int) C4BandId::Open]   = { 0.028f, 0.40f, -12.0f, 0.85f, 1.0f };

    p.coupling = { true, 0.50f, 0.32f, 1.1f, 1.6f };
    p.halo     = { 0.5f, 1.0f, 0.90f };

    // Coupling amount — Phase 5 measured decision: the Phase 3/4 candidate
    // (strength 0.20-0.35, max 0.8-1.0) measured 0.01-0.06 dB of valley fill
    // even at +15/+15 — BELOW the perceptual floor (the parallel skirts
    // compress the contour ~2.5x). The production amount was raised so the
    // strongest overlapping boosts land a ~0.1-0.3 dB contour: "felt more
    // than consciously heard", while every bound (<= 0.7 dB fill, no peak
    // stealing) stays green. See docs/C4_PHASE5_PERCEPTUAL_TUNING.md.

    p.bloomDefault      = 0.40f;   // BLOOM 4.0 in user units (0..10)
    p.autoGainStrength  = 1.0f;
    p.bloomLawExponent  = 0.85f;
    p.residualOversample = 2;
    return p;
}

} // namespace C4
} // namespace APEX
