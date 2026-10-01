# C4 Phase 4 — BLOOM Residual Bank (ENGINEERING PASS)

Status: **C4 PHASE 4 — ENGINEERING PASS** — declared 2026-08-12. Phases 1–3
remain frozen baselines. This phase is engineering-only: NO sonic candidate is
declared "the C4 sound" — that is Phase 5.

## ARCHITECTURE

Per band, per channel, per sample (allocation-free, zero added latency):

```
linear C4 output (Phase 1-3, untouched)
  + per band:
      exc      = the band's UNITY shape tap (band-focused signal, EQ gain
                 never double-applied)
      u        = exc * 10^(-driveRefDb/20)          (calibrated drive)
      oddRes   = tanh(u) - u                        (bounded odd shaper minus
                 its linear component = harmonic residual only)
      evenRes  = dcBlock( tanh(u)^2 )               (bounded even shaper;
                 DC deliberately managed by a 15 Hz one-pole HPF on the even
                 residual ONLY — measured DC = -374 dB rel. fundamental)
      residual = residualLevel * activation * bloomDrive
                 * ( evenWeight*evenRes + (1-evenWeight)*oddRes )
  + coupling (Phase 3) and HALO (Phase 2) unchanged
```

- **BLOOM = 0 is the sacred baseline**: `bloomDrive = 0` skips the entire
  bank — bit-identical to the colorless reference for EVERY variant under
  shaped settings (measured), zero nonlinear work, zero extra phase/gain/
  latency, no allocations, unchanged neutral/coupling behavior.
- **Activation follows positive EQ intent** from the SMOOTHED gains:
  `g>0 -> (g/15)^activationExp`; `g<0 -> suppressed by cutSuppression`
  (variant values = 1.0 -> cuts essentially clean); `g==0 -> exactly 0`.
- **BLOOM control law**: `residual = bloomNorm^bloomLawExponent` (candidate,
  per profile; exactly 0 at BLOOM 0).

## WAVESHAPER CANDIDATES (evaluated)

- S1 `tanh` (odd, symmetric, bounded [-1,1], H3-leaning, no DC).
- S2 `tanh^2` (even, bounded [0,1], H2-leaning, DC managed by the blocker).
- The production residual = evenWeight·S2-residual + (1-evenWeight)·S1-residual.
- Unbounded polynomials were rejected by design (no unbounded polynomial in
  the production path; operating range is mathematically constrained by tanh).

## HARMONIC DATA (measured, 48 kHz, BLOOM 10, +6 dB band, -6 dBFS input)

| Band | H2 | H3 | THD | DC | -30 dBFS THD |
|------|----|----|-----|----|--------------|
| WEIGHT (100 Hz, evenW 0.75) | -45.0 dB | -63.3 dB | -44.6 dB | -374 dB | -57.8 dB |
| SCULPT (1 kHz, evenW 0.65) | -46.2 dB | -60.3 dB | -45.6 dB | -374 dB | -59.0 dB |
| BITE (4 kHz, evenW 0.50) | -49.3 dB | -58.2 dB | -48.4 dB | -374 dB | -62.2 dB |
| OPEN (5 kHz, evenW 0.40) | -56.4 dB | -61.6 dB | -55.0 dB | -374 dB | -69.2 dB |

- Band character: WEIGHT even/H2-dominant and heaviest; BITE balanced
  H2/H3; OPEN lowest total (sheen only) — matches the tonal intent.
- Level dependence: -30 dBFS is ~13 dB cleaner than -6 dBFS (subtle
  coloration in the normal operating region).
- **Boosts vs cuts**: cut THD is at the numerical noise floor (cutSuppression
  1.0) — the suite asserts >= 20 dB separation between boost and cut
  residual for every band (ADDING TONE ADDS COLOR; REMOVING TONE STAYS
  CLEANER).
- BLOOM 0: H2/H3 below -60 dB (linear) for every band.

## TRANSIENT DATA

Kick-like transient (60 Hz decaying burst, 0.5 s): crest factor BLOOM 0 vs
medium vs 10 preserved within 0.5 (measured PASS). The residual is harmonic-
level (tens of dB below the signal), so C4 does not behave like a compressor
at any BLOOM setting.

## ANTIALIAS BENCHMARK (measured, 48 kHz, OPEN +15, BLOOM 10, 7 kHz input)

Folded-alias bins (13/20 kHz — the H4/H5 fold targets):

| Strategy | Fold-bin level |
|----------|----------------|
| 1x reference | -67.2 dB |
| 2x (residual path only) | -70.1 dB |
| 4x (residual path only) | -71.8 dB |

- The 1x alias is already far below audibility (-67 dB); oversampling
  reduces it monotonically. Only the NONLINEAR residual path pays the
  multirate cost; the validated linear EQ is never oversampled.
- Zero added latency (causal backward-linear upsample + averaging decimate).

## REALTIME / PERFORMANCE

- Zero realtime allocations with BLOOM active (UnitTestAllocationChecker,
  variant C, all bands +15, BLOOM sweep) — PASS.
- Zero added latency (`getLatencySamples() == 0`) — PASS.
- Oversampling cost scales with `residualOversample` (1/2/4), confined to
  the residual path. Instance-count CPU benchmarks are deferred to the
  Phase 9 performance annex (Spectrum Flag does not exist yet; analyzer
  cost is irrelevant in this phase).

## A/B/C PROFILES (valid controlled experiments; NOT frozen)

| Dimension | A | B | C |
|-----------|---|---|---|
| residualLevel (W/S/B/O) | 0 / 0 / 0 / 0 | .05/.05/.045/.025 | .07/.07/.06/.035 |
| evenWeight (W/S/B/O) | .6 all | .75/.65/.50/.40 | .80/.70/.55/.45 |
| activationExp | 1.0 | 1.0 | 0.8 |
| bloomLawExponent | 1.0 | 1.0 | 0.8 |
| residualOversample | 1 | 2 | 4 |
| (coupling, HALO, geometry unchanged from Phase 2/3 candidates) |

A = the conservative no-color reference; B = the balanced candidate; C = the
stronger/softer-activation candidate. Only Phase 1-3-validated architecture
dimensions are exposed; A/B/C never change the frozen parameter contract.

## REGRESSIONS

- APEX.C4 Debug: GREEN (exit 0).
- APEX.C4 Release: GREEN (exit 0) — no Debug-vs-Release discrepancy.
- Global Debug regression: GREEN (exit 0) — manifest `b645daf0b946`.
- Global Release regression: GREEN (exit 0) — manifest `acac2f634850`.
- Repository policy: exit 0. Dependency verification: exit 0.

## OPEN VARIABLES (deferred to Phase 5 — deliberately NOT frozen)

- Final residual level, per-band evenWeight, drive calibration (driveRefDb),
  activation exponent, cut suppression, BLOOM control law, oversample
  strategy, and the production BLOOM default.
- Phase 5 performs level-matched A/B/C listening across the required source
  corpus (vocals, bass, drums, guitars, mix bus), the C4 BOOM TEST
  (WEIGHT +3, SCULPT +2, BITE +2, OPEN +3, level-match ON), and freezes the
  final tuning only when bypass makes the signal feel SMALLER.
