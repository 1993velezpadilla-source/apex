# C4 Phase 2 — HALO Shelf Mapping (PASS)

Status: **PHASE 2 = PASS** — declared 2026-08-12. Phase 1 remains the frozen
regression baseline (`docs/C4_PHASE1_EVIDENCE.md`); this document records the
Phase 2 HALO architecture, its measured validation, and the explicit
distinction from the LPF's temporary behavior.

## Architecture

`C4HaloShelfMapper` (`Source/C4Core/C4HaloShelfMapper.h`) translates the OPEN
band's HALO (high-shelf) control frequency — range 2.5..40 kHz
(`kHaloFreqMinHz..kHaloFreqMaxHz`), deliberately beyond the audio range — into
a REALIZABLE in-band shelf frequency. Motivation: a raw 40 kHz control at
44.1 kHz would compute `g = tan(pi*40000/44100) < 0` — an unrealizable
negative-g SVF.

Mapping formula (all values float, allocation-free, realtime-safe):

```
s       = clamp(mappingSoftness, 0, 1)             # defensive clamp
ratio   = clamp(maxRealizableRatio, 0.01, 1)
ceiling = ratio * 0.5 * fs                          # 0.90 * Nyquist (default)
c       = clamp(controlHz, 2.5k, 40k)

if s <= 0:        return min(c, ceiling)            # EXPLICIT hard-clamp branch
knee    = ceiling * (1 - 0.20 * s)
if c <= knee:     return c                          # 1:1 below the knee
t       = (c - knee) / max(1, 40k - knee)           # guarded division
k       = 2 + 8 * (1 - s)
decay   = exp(-k * t)
return ceiling - (ceiling - knee) * decay           # asymptotic approach
```

Branch behavior:
- **softness = 0**: mathematically defined straight/hard-clamp contract
  (`min(c, ceiling)`) — an explicit branch with NO division, no degenerate
  range dependence. Verified exactly at every rate and control value.
- **softness = 1**: identity ends at 0.8·ceiling; the realized shelf gives up
  progressively as the control rises (the gentlest response).
- **Degenerate inputs are total**: softness clamps to [0,1] (so −1 behaves as
  0 and 2 as 1), ratio clamps to (0,1]; every division is guarded; no NaN/Inf
  for any input.

## Engine integration (`C4BandCore::C4BandShared::advance`)

1. Every band's smoothed frequency is clamped to the realizable bound
   `0.49 * fs` (the same safety bound the HPF/LPF use) BEFORE any further
   processing. This is a no-op for every in-range control and closes a
   measured defect: the blend-weighted HALO morph interpolates between the raw
   control and the mapped value, so the raw control itself must be realizable
   (the automation torture caught intermediate `g < 0` states at 44.1/48 kHz
   before the clamp existed).
2. The OPEN band (HighShelf kind) applies the mapper blend-weighted by
   `modeBlend_`: blend 0 = bell at the raw control (unmapped — required),
   blend 1 = HALO shelf at the mapped frequency, every intermediate blend a
   convex combination of two realizable endpoints (continuous morph, no
   topology change to the proven Phase 1 linear DSP).

## Measured validation (all six rates: 44.1/48/88.2/96/176.4/192 kHz)

| Contract | Measured | Test |
|----------|----------|------|
| 1:1 below the knee | exact at every rate | C4.HaloMapper identity |
| Continuous at the knee | exact | C4.HaloMapper monotonic/continuity |
| Monotonic non-decreasing | verified over the full control range | C4.HaloMapper |
| Bounded by 0.90·Nyquist | verified at every rate/control | C4.HaloMapper bounded |
| 40 kHz control valid at every rate | finite, in-band, ≤ ceiling | C4.HaloMapper degenerate |
| softness 0 = hard clamp | exact `min(c, ceiling)` | C4.HaloMapper |
| Bell mode unmapped | 20 kHz control → exactly 20 kHz | C4.HaloMapper engine integration |
| HALO 20 kHz @ 44.1 kHz | maps to ~18.73 kHz (softness 0.5) | C4.HaloMapper |
| Morph 0.5 | lerped frequency ~19.37 kHz | C4.HaloMapper |
| Automation torture (Bell 20k ↔ HALO 40k, freq sweep while morphing) | finite output, legal Fc (0 < f ≤ 0.49·fs), bounded, no click (max delta < 1.5), all six rates | C4.HaloMapper torture |

## OPEN/HALO vs LPF — explicit distinction

- The HALO mapper governs the **OPEN band's shelf mode only**. It does NOT
  alter the LPF.
- The LPF's `0.49 * Nyquist` clamp (the 24 kHz control at 44.1 kHz behaving
  as ~21.6 kHz, near-flat in-band) remains a **separate, documented Phase 1
  limitation**, pinned by `C4.Filters / LPF 24 kHz at 44.1 kHz: clamped
  TEMPORARY behavior`. It is not solved by HALO and remains TEMPORARY until a
  dedicated high-frequency LPF mapping is designed (outside HALO scope).

## Evidence manifests

- APEX.C4 Debug with HALO: manifest `d2f1b0e78181` (pre-hardening), full
  green after hardening (Debug category run, exit 0).
- APEX.C4 Release with HALO: exit 0 (post-hardening).
- Global Release regression with HALO: exit 0 — manifest `56dc0696620b`.
- Policy: `test_repository_policy.ps1` exit 0; `verify_dependencies.ps1`
  exit 0 (after adding `C4HaloShelfMapper.h`, `C4HaloMapperTests.cpp`, and
  this document).

## Remaining Phase 5 tuning variables (NOT frozen)

- `C4HaloTuning::amount` — HALO character gain. Phase 2 maps frequency only;
  the amount is reserved for Phase 5 perceptual calibration.
- `C4HaloTuning::mappingSoftness` — currently 0.5 in all variants (A/B/C);
  the law is verified for 0..1 but the production value is a Phase 5
  listening decision.
- The knee geometry (`0.20 * softness`) and approach rate (`2 + 8*(1-s)`) are
  documented candidates, tunable without touching the parameter contract.
