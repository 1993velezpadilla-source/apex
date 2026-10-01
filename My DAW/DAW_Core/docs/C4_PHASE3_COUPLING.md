# C4 Phase 3 — BLOOM Coupling / Geometry (PASS)

Status: **PHASE 3 = PASS** — declared 2026-08-12. Phase 1 remains the frozen
regression baseline; Phase 2 (HALO) is closed (`docs/C4_PHASE2_HALO.md`).

## Architecture (frozen product intent)

- **Adjacent pairs only**: WEIGHT<->SCULPT, SCULPT<->BITE, BITE<->OPEN.
  There is NO direct WEIGHT<->OPEN coupling.
- **Boost + boost only**: coupling engages only when BOTH bands of a pair are
  boosted (gainDb > 0). Cut+cut and boost+cut coupling are NOT implemented
  (reserved for later listening evidence).
- **Contour geometry, not subtraction**: coupling NEVER reduces the user's
  requested peak gain and never subtracts fixed dB. It adds a smooth contour
  bell in the VALLEY between the facing shoulders of the two boosted bands —
  two nearby boosts feel like one intentional musical shape.

## Implementation (`C4EngineCore`)

Per adjacent pair, computed once per sample (shared control state — mono ==
stereo-left determinism):

```
octaveSep = |log2(fA / fB)|
overlap   = 1 - octaveSep / 5            # 5 octaves of separation = no overlap
engage    = (overlap - overlapThreshold) / (1 - overlapThreshold)   # >0 only
contourDb = maxContourDb * strength * (gA/15) * (gB/15) * engage
contourFreq = exp2((log2 fA + log2 fB) / 2)      # log midpoint
```

- The contour gain is smoothed per sample (C4Smoother, gainMs time) —
  click-free by construction.
- The contour SVF (per channel, like the band SVFs) is a bell at the log
  midpoint with Q = `contourQ`, added in parallel: `x += (A-1) * band`.
- The contour is EXACTLY zero when either band is at/under 0 dB or the
  overlap is below the threshold — the uncoupled response is bit-identical
  whenever coupling is inactive (measured).

## Candidates (via C4TuningProfile — amount NOT frozen)

| Variant | enabled | strength | overlapThreshold | contourQ | maxContourDb |
|---------|---------|----------|------------------|----------|--------------|
| A       | false   | 0.0      | 0.35             | 1.2      | 0.8          |
| B       | true    | 0.20     | 0.35             | 1.2      | 0.8          |
| C       | true    | 0.35     | 0.30             | 1.0      | 1.0          |
| P1      | false   | 0.20     | 0.35             | 1.2      | 0.8          |

`P1` = `makeProfilePhase1Reference()` — the FROZEN Phase 1 reference:
identical to variant B (geometry, smoothing, HALO) with coupling OFF. Every
Phase 1 regression test runs on P1, so the Phase 1 uncoupled response is
preserved as the regression baseline while A/B/C are measured against it.
Final sonic amount: Phase 5 listening.

## Measured validation (C4.Coupling)

| Contract | Measured | Result |
|----------|----------|--------|
| Coupling inactive bit-identical to Phase 1 | single boost and all-cut configs: 0 bit diffs | PASS |
| Valley fill between adjacent boosts | coupled >= uncoupled at the log midpoint, fill <= 0.6 dB (cap) | PASS |
| Contour localized | responses coincide away from the pair (< 0.05 dB at 20 kHz) | PASS |
| No pathological peak | coupled <= uncoupled + 0.6 dB on the full grid | PASS |
| No unexpected cancellation | coupled min >= uncoupled min - 0.6 dB (extreme +15/+15) | PASS |
| Center gains preserved | within 0.6 dB of the uncoupled response at each center | PASS |
| Unequal boosts (W +9, S +3) | proportional fill, bounded 0..0.5 dB | PASS |
| Small boosts (+1 each) | gentle fill, bounded 0..0.3 dB | PASS |
| Every adjacent pair, far/slight/medium/heavy/identical | 15 configurations; far = no coupling (< 0.1 dB), others fill within cap | PASS |
| Phase stability | coupled phase at the valley within 0.5 rad of uncoupled | PASS |
| All six sample rates | valley fill within cap at every rate | PASS |
| Direct engine vs processor agreement | 0 float diffs with coupling active (80 blocks) | PASS |
| Realtime allocations | zero allocation under variant C with all four bands boosted (UnitTestAllocationChecker) | PASS |
| Automation | SCULPT gain swept 0->15->0 repeatedly: finite, bounded (max 2.38 within the 7.6 envelope), no click (max delta < 4.0) | PASS |
| Lifecycle/reset | 3 reset cycles bit-identical with coupling active | PASS |

## Evidence

- APEX.C4 Debug (Phase 3): green, exit 0.
- APEX.C4 Release: green, exit 0 (no Debug-vs-Release discrepancy).
- Global Debug regression: green, exit 0 — manifest `dadd228b6d74`.
- Global Release regression: green, exit 0 — manifest `60ca673137af`.
- Policy: `test_repository_policy.ps1` exit 0; `verify_dependencies.ps1`
  exit 0 (after `C4CouplingTests.cpp`, the coupling engine code, and this
  document).

## Remaining Phase 5 tuning (NOT frozen)

- Coupling `strength` (0.20/0.35 candidates), `contourQ`, `maxContourDb`,
  `overlapThreshold`, and the 5-octave overlap span — all tunable via
  `C4TuningProfile` without touching the parameter contract.
- The per-band `C4BandGeometry` Q-drive laws (WEIGHT broad/heavy, SCULPT
  strongest proportional-Q, BITE closer to constant-Q/incisive, OPEN
  musical/smooth incl. HALO) are implemented candidates; the exact laws are
  Phase 5 decisions. Boosts BLOOM / cuts LOCK is preserved as the design
  principle — automatic behavior never overrides the user's intention.
