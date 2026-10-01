# C4 Phase 5 — Perceptual Tuning / Final Sonic Design (PASS)

Status: **C4 PHASE 5 — PASS** — declared 2026-08-13. Phases 1–4 remain frozen
baselines. This document records the final production tuning, the measured
evidence behind every decision, and the explicit boundary between
MEASURED FACT and DESIGN DECISION.

Labels used throughout:

- **MEASURED FACT** — an observed, reproducible measurement from the
  `C4.Phase5Tuning` suite (Debug + Release, category `APEX.C4`).
- **DESIGN DECISION** — a deliberate product choice made under the delegated
  design authority, informed by professional precedent, the C4 North Star,
  and the measured data. Not proof of subjective superiority.

---

## 1. Process summary

The Phase 5 tuning used staged isolation: (A) band geometry, (B) BLOOM
coupling, (C) HALO mapping, (D) BLOOM color, (E) BLOOM control law/default,
(F) the combined production profile. Candidates A/B/C (historical) and a
provisional hybrid production profile were measured by the new
`C4.Phase5Tuning` suite; candidates that violated engineering bounds were
eliminated or adjusted with a targeted, measured rationale (below).

The repository contains only ONE legitimate real-musical source
(`My DAW/DAW_Core/stftPitchShift-main/examples/voice.wav`). No bass, drum,
guitar, or mix-bus corpus exists in this workspace. Therefore:

- All engineering-side tuning decisions were completed from measurement +
  precedent + musical-purpose criteria.
- The listening side is supported by a deterministic, level-matched render
  pack (Section 8). No subjective claim about instruments that were never
  auditioned is made in this document.

---

## 2. Final production profile (FROZEN)

`makeProfileProduction()` in `Source/C4Core/C4TuningProfile.h`:

| Variable | Final value |
|---|---|
| WEIGHT geometry | baseBellQ 0.75, baseShelfQ 0.60, boostQDrive 0.50, cutQDrive 0.80, qLawExponent 1.20, proportional ON |
| SCULPT geometry | baseBellQ 0.80, baseShelfQ 0.70, boostQDrive 1.10, cutQDrive 1.30, qLawExponent 0.90, proportional ON |
| BITE geometry | baseBellQ 1.00, baseShelfQ 0.80, boostQDrive 0.30, cutQDrive 0.45, qLawExponent 1.00, proportional ON |
| OPEN geometry | baseBellQ 0.80, baseShelfQ 0.60, boostQDrive 0.35, cutQDrive 0.40, qLawExponent 1.00, proportional ON |
| WEIGHT color | residualLevel 0.055, evenWeight 0.75, driveRefDb −12, activationExp 0.85, cutSuppression 1.0 |
| SCULPT color | residualLevel 0.055, evenWeight 0.65, driveRefDb −12, activationExp 0.85, cutSuppression 1.0 |
| BITE color | residualLevel 0.050, evenWeight 0.50, driveRefDb −12, activationExp 0.85, cutSuppression 1.0 |
| OPEN color | residualLevel 0.028, evenWeight 0.40, driveRefDb −12, activationExp 0.85, cutSuppression 1.0 |
| Coupling | enabled, strength 0.50, overlapThreshold 0.32, contourQ 1.1, maxContourDb 1.6 |
| HALO | mappingSoftness 0.5, amount 1.0 (unity), maxRealizableRatio 0.90 |
| BLOOM law | bloomLawExponent 0.85 |
| BLOOM default | **4.0** (user units 0..10) |
| Antialias | residualOversample **2x** (residual path only, zero added latency) |
| Auto Gain | autoGainStrength 1.0 (parameter default OFF) |
| Smoothing | unchanged (Phase 1 validated) |

A/B/C and the Phase 1 reference (`makeProfilePhase1Reference`) remain in the
code as documented historical candidates; the Phase 1–4 regression tests keep
running on them.

---

## 3. Band geometry — final selections

Half-excess bandwidth (octaves) at +3/+6/+12 dB boost and −6/−12 dB cut,
bell mode, measured at 48 kHz (parallel-topology metric: width where the
boost EXCESS falls to half; a parallel boost never returns to 0 dB, so the
classic "−3 dB from peak" width is undefined for small boosts — see the
suite comment).

MEASURED FACT (production profile `P`, geometry table):

| Band | +3 dB | +6 dB | +12 dB | −6 dB | −12 dB |
|---|---|---|---|---|---|
| WEIGHT (100 Hz) | 1.83 | 1.83 | 1.78 | 1.06 | 0.32 |
| SCULPT (400 Hz) | 1.49 | 1.38 | 1.25 | 0.81 | 0.24 |
| BITE (2.5 kHz) | 1.40 | 1.43 | 1.48 | 0.84 | 0.28 |
| OPEN (10 kHz) | 1.25 | 1.27 | 1.30 | 0.79 | 0.27 |

Candidate comparison (same metric): A is constant-Q on three bands and its
boost width GROWS with gain (e.g. BITE 1.47→1.80 oct) — boomy/unfocused
when pushed; C narrows strongly (SCULPT +12 = 1.13 oct — surgical).

DESIGN DECISIONS:
- **WEIGHT: late-proportional law (drive 0.50, exponent 1.20).** Small and
  moderate boosts stay ~1.8 octaves wide ("size without mud"); the focus
  arrives only at extreme gains. Cuts are tighter than boosts (0.80) for
  controlled low-end cleanup. Rejects A's gain-growing width (mud risk).
- **SCULPT: progressive proportional-Q (1.10/1.30 @ 0.90).** 1.49→1.25 oct
  across the gain range: broad and forgiving for small moves, progressively
  focused when pushed — "body without boxiness", difficult to make ugly.
  Rejects C (1.13 oct at +12 = surgical) and A (constant, 1.97 at +6 = blur).
- **BITE: near-constant-Q (0.30/0.45 @ 1.00).** Measured widths 1.40/1.43/
  1.48 — essentially flat. Professional precedent: incisive presence bands
  favor predictable constant-Q behavior; BITE is the most assertive band and
  must never turn surgical or veiled. The small drive keeps +15 dB usable.
- **OPEN: gentle proportional (0.35/0.40).** 1.25–1.30 oct, stable; the HALO
  shelf carries the top-end identity (Section 5).

---

## 4. BLOOM coupling — final selection

MEASURED FACT (valley fill at the log midpoint, dB; PROD = final):

| Pair | Profile | +6/+6 | +9/+3 | +15/+15 |
|---|---|---|---|---|
| WEIGHT↔SCULPT | B | 0.01 | 0.00 | 0.02 |
| WEIGHT↔SCULPT | C | 0.02 | 0.01 | 0.06 |
| WEIGHT↔SCULPT | **PROD** | **0.03** | **0.02** | **0.11** |
| SCULPT↔BITE | **PROD** | 0.02 | 0.02 | 0.09 |
| BITE↔OPEN | **PROD** | 0.03 | 0.02 | 0.12 |

All fills within the 0.7 dB inflation cap; never negative (no peak stealing);
the Phase 3 architecture contracts remain green on the historical profiles.

DESIGN DECISION: the Phase 3/4 candidate amounts (strength 0.20–0.35,
maxContourDb 0.8–1.0) measured 0.01–0.06 dB even at +15/+15 — BELOW the
perceptual floor (the parallel skirts compress the contour ~2.5×), which
contradicts the North Star ("felt more than consciously heard" — a contour
must at least be felt). The production amount was raised (strength 0.50,
maxContourDb 1.6) so strong overlapping boosts land a ~0.1 dB contour while
every bound stays green: small moves remain sub-JND (0.02–0.03 dB), extreme
adjacent boosts get a felt-not-heard valley contour, and there is no gain
inflation or "processed" contour. This was the only Phase 5 constant change
driven by a measured shortfall; it was confined to the coupling variables.

---

## 5. HALO — final selection

MEASURED FACT (realized shelf frequency, production softness 0.5,
maxRealizableRatio 0.90):

| Control | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| 5 kHz | 5000 | 5000 | 5000 | 5000 |
| 10 kHz | 10000 | 10000 | 10000 | 10000 |
| 20 kHz | 18734 | 19766 | 20000 | 20000 |
| 25 kHz | 19558 | 21174 | 25000 | 25000 |
| 30 kHz | 19771 | 21501 | 30000 | 30000 |
| 40 kHz | 19840 | 21595 | 43189 | 40000 |

Monotonic non-decreasing at every rate/softness (asserted); bounded by
0.90·Nyquist; identity below the knee. The progression is exactly the North
Star: 5–10 kHz literal brightness/sheen, 20 kHz→air, 40 kHz→openness —
higher control values keep adding (no dead zone, no ineffectiveness).

DESIGN DECISION: `mappingSoftness` stays 0.5 (the Phase 2-validated law).
`amount` is **frozen at 1.0 (unity)**: the HALO identity is the control-
frequency mapping itself, never an added-gain trick — a HALO amount above
unity would inflate loudness and violate "air without glass" honesty
(and the no-loudness-cheating rule of the BOOM test).

---

## 6. BLOOM color — final selection

MEASURED FACT (BLOOM 10, 48 kHz, −6 dBFS sine, per band; PROD = final):

| Band | Profile | +3 dB H2/H3/THD | +6 dB H2/H3/THD | +15 dB H2/H3/THD | DC |
|---|---|---|---|---|---|
| WEIGHT (100 Hz) | B | −48.1/−66.3/−47.6 | −45.0/−63.3/−44.6 | −46.1/−64.3/−45.6 | −371..−383 |
| WEIGHT | C | −42.7/−64.2/−42.4 | −40.9/−62.4/−40.6 | −43.6/−65.0/−43.2 | ≤−370 |
| WEIGHT | **PROD** | **−45.1/−63.4/−44.7** | **−43.0/−61.2/−42.6** | **−45.3/−63.5/−44.8** | ≤−370 |
| SCULPT (1 kHz) | **PROD** | −46.3/−60.4/−45.7 | −44.1/−58.2/−43.6 | −46.4/−60.5/−45.8 | ≤−370 |
| BITE (4 kHz) | **PROD** | −49.3/−58.2/−48.4 | −47.2/−56.0/−46.2 | −49.4/−58.3/−48.5 | ≤−370 |
| OPEN (5 kHz) | **PROD** | −56.4/−61.5/−54.9 | −54.2/−59.4/−52.8 | −56.5/−61.7/−55.0 | ≤−370 |

Every cell: THD below the −30 dB saturator ceiling; DC below −250 dB
(measured −370..−383); the band hierarchy is exactly the intended identity:
WEIGHT warmest/even-dominant, SCULPT rich, BITE balanced H2/H3, OPEN least
nonlinear ("minimal sheen"). Cuts remain at the noise floor (cutSuppression
1.0, ≥20 dB boost/cut separation — Phase 4 suite, still green).

MEASURED FACT (production, BLOOM sweep, +6 dB, −6 dBFS — THD):

| Band | BLOOM 2 | BLOOM 3 | BLOOM 4 | BLOOM 5 | BLOOM 6 |
|---|---|---|---|---|---|
| WEIGHT | −54.5 | −51.5 | −49.3 | −47.7 | −46.3 |
| SCULPT | −55.5 | −52.5 | −50.4 | −48.7 | −47.4 |
| BITE | −58.2 | −55.2 | −53.0 | −51.4 | −50.0 |
| OPEN | −64.7 | −61.7 | −59.6 | −57.9 | −56.6 |

Strictly monotonic; level-dependent (≈13 dB cleaner at −30 dBFS, Phase 4).

DESIGN DECISIONS:
- residualLevel 0.055/0.055/0.050/0.028 — B-like hierarchy, slightly raised
  so the default is "audible enough to matter" without C's stronger levels
  (C measured −40..−43 THD at +6 — too close to an obvious "saturator").
- evenWeight 0.75/0.65/0.50/0.40 — WEIGHT density/warmth (H2-leaning),
  SCULPT body/glue, BITE balanced edge, OPEN sheen-only.
- activationExp 0.85 — a whisper of color on small boosts without turning
  C4 into a harmonic exciter; cuts fully suppressed (1.0).
- driveRefDb −12 kept (Phase 4 calibrated reference).

---

## 7. Antialias strategy — final decision

MEASURED FACT (worst audible-band fold product, OPEN +15 dB, BLOOM 10,
−6 dBFS; PROD geometry/color with the OS factor swept):

| Rate | f0 | 1x | 2x (PROD) | 4x |
|---|---|---|---|---|
| 44.1 kHz | 10 kHz | −59.0 | −61.7 | −63.4 |
| 44.1 kHz | 12 kHz | −55.8 | −56.5 | −57.1 |
| 44.1 kHz | 15 kHz | −55.8 | −56.5 | −57.1 |
| 44.1 kHz | 18 kHz | −55.8 | −56.5 | −57.1 |
| 48 kHz | 10 kHz | −59.0 | −61.7 | −63.4 |
| 48 kHz | 12 kHz | n/a (f0 = fs/4: all folds land on f0/DC/Nyquist — no audible product) | | |
| 48 kHz | 15 kHz | −55.8 | −56.5 | −57.1 |
| 48 kHz | 18 kHz | −54.6 | −55.7 | −56.5 |

DESIGN DECISION: **freeze 2x.** Rationale: the elimination floor (−55 dB on
the worst audible fold at the absolute extreme settings) is MET by 2x at
every measurable cell and MISSED by 1x at 18 kHz/48 kHz (−54.6) — the
evidence the strategy decision needed. 4x buys only +0.8 dB more at the
worst cell while doubling the residual-path cost again; the linear EQ is
never oversampled, and zero added latency is preserved at every factor
(causal backward-linear upsample + averaging decimate). 2x is the best
practical balance of alias cleanliness, CPU, and multi-instance scalability.
(Phase 4's 7 kHz benchmark, −67..−72 dB, remains the low-mid reference.)

---

## 8. C4 BOOM TEST — measurement + render pack

Settings: WEIGHT +3 dB @100 Hz, SCULPT +2 dB @400 Hz, BITE +2 dB @2.5 kHz,
OPEN +3 dB @10 kHz (bell modes), no compressor/limiter/saturation/makeup.

MEASURED FACT (production profile):

| Quantity | Value |
|---|---|
| Contour at 100 Hz | +3.14 dB |
| Contour at 400 Hz | +2.34 dB |
| Contour at 2.5 kHz | +2.25 dB |
| Contour at 10 kHz | +3.08 dB |
| RMS delta (program-like signal, vs bypass) | +1.86 dB |
| Peak delta | +2.17 dB |
| Kick-burst crest, BLOOM 0 / 4 / 10 | 11.60 / 11.61 / 11.64 dB |

- Crest delta ≤ 0.04 dB at every BLOOM setting — **C4 is not a compressor**;
  transients are preserved (transient-preservation bound: ≤ 0.5 dB).
- No candidate can win by loudness: the level-match compensation for the
  render pack is the measured RMS delta, applied as output trim — no limiter.

RENDER PACK (deterministic, produced by `C4.RenderPack` in the APEX.C4
suite; FINAL RUN 2026-08-13): renders `stftPitchShift-main/examples/voice.wav`
(44.1 kHz, mono, 274432 frames — the only real musical source in the
repository) to `My DAW/DAW_Core/evidence/renderpacks/C4_PHASE5/`:

| File | Settings |
|---|---|
| `01_REF_bypass.wav` | C4 bypassed (reference) |
| `02_C4_BLOOM0_FLAT.wav` | production, BLOOM 0, all bands 0 dB |
| `03_C4_BOOM_BLOOM0.wav` | BOOM settings, BLOOM 0 (linear contour only) |
| `04_C4_BOOM_BLOOM4.wav` | BOOM settings, BLOOM 4.0 (production default) |
| `05_C4_BOOM_BLOOM4_MATCHED.wav` | BOOM settings, BLOOM 4.0, output trim = −(source-measured RMS delta) — the honest level-matched comparison |
| `06_C4_BOOM_BLOOM10.wav` | BOOM settings, BLOOM 10 (high reference) |
| `RENDER_MANIFEST.md` | source info, block size, exact settings, measured RMS/peak/crest per render |

MEASURED FACT (render source — real vocal):

- 02 (BLOOM 0, flat) vs 01: RMS 0.00 dB, peak 0.00 dB — the neutral wire is
  bit-identical on real audio at the production default geometry.
- 04 (BOOM, BLOOM 4) vs 01: RMS +1.56 dB, peak +1.65 dB.
- 06 (BOOM, BLOOM 10) vs 01: RMS +1.55 dB, peak +1.62 dB.
- 05 (matched) vs 01: RMS −0.00 dB (trim −1.559 dB), peak +0.09 dB.

Note on the tuning-suite synthetic numbers (+1.86 dB RMS / +2.17 dB peak on
the program-like signal) versus the render-source numbers (+1.56 / +1.65):
the difference is the source spectrum (the synthetic signal carries more
low-frequency energy where the WEIGHT boost acts); the render pack applies
its OWN source-measured compensation (−1.559 dB), which is the honest
per-source level match. No limiter, no compressor, no makeup trick in any
render.

The listening pass criterion remains: level-matched, bypass should make the
signal feel smaller. **That judgment is a human activity and is not claimed
from measurement.** The pack is the reproducible evidence/support material.

---

## 9. BLOOM default 4.0

MEASURED FACT (production, BLOOM 4.0, +6 dB, −6 dBFS hot sine):

| Band | H2 | H3 | THD |
|---|---|---|---|
| WEIGHT | −49.8 | −68.0 | −49.3 |
| SCULPT | −50.9 | −65.0 | −50.4 |
| BITE | −54.0 | −62.8 | −53.0 |
| OPEN | −61.0 | −66.2 | −59.6 |

On real program material (≈10–20 dB below the hot sine per band), the
default lands in the mid-50s-to-60s — the "subtle but recognizable" band.

DESIGN DECISION: freeze **BLOOM 4.0** (not 5.0, not 3.0): it exposes the
identity at insertion, sits below the "obviously saturated" region
(measured sweep: WEIGHT THD −49.3 at 4 vs −44.6 at 10), is transient-safe
(crest delta 0.01 dB), and leaves headroom for the user. The BLOOM law is
nonlinear (exponent 0.85) so low settings keep meaningful color resolution.

MEASURED FACT (neutral path): a flat production instance (BLOOM 4.0, all
gains 0 dB) settles into the neutral fast path and remains bit-identical to
the wire (the Phase 5 gate semantics: BLOOM is not a neutrality condition —
residual activation is exactly 0 at 0 dB gain; the bloom smoother's own
settlement is still required). See Section 11.

---

## 10. Validation summary (all green — FINAL Phase 5 closure run)

| Gate | Result |
|---|---|
| APEX.C4 Debug (final) | GREEN — manifest `230cb5c882e5` |
| APEX.C4 Release (final) | GREEN — manifest `dc511cb40a80` (no Debug/Release discrepancy) |
| Global Debug (final) | GREEN — manifest `7658c94d3938` |
| Global Release (final) | GREEN — manifest `d365fd640f5b` |
| Repository policy | PASS (exit 0) |
| Dependency verification | PASS (exit 0) |
| C4.RenderPack | GREEN — render pack + manifest written (see Section 8) |
| Harmonic suite / boost-vs-cut / transients / numerical safety / automation / RT allocation / all six rates / lifecycle / coupling / HALO / neutral-path | all GREEN inside APEX.C4 |

Technical elimination bounds verified by `C4.Phase5Tuning`:
alias (2x) ≤ −55 dB · THD < −30 dB · DC < −250 dB · crest Δ ≤ 0.5 dB ·
coupling fill ≤ 0.7 dB, ≥ 0 · six-rate gain variation ≤ 0.5 dB (measured
±0.01 dB) · BLOOM 0 bit-identical to the colorless reference (production
profile) · zero realtime allocations · zero added latency · finite states
under torture.

## 11. Known limitations (honest scope)

- The repository's real-musical corpus is ONE vocal file; no bass, drums,
  guitars, buses, or mixes were available. The render pack and the BOOM
  measurement pipeline are complete; the final subjective listening pass on
  a full corpus remains a human activity, unclaimed here.
- CPU/per-instance cost of the 2x residual path is deferred to the Phase 9
  performance annex (the residual bank early-outs at BLOOM 0 and at 0 dB
  gains; the flat-instance neutral fast path is preserved).
- A flat instance at BLOOM 4.0 runs the engine for the first ~125 ms after
  prepare until the bloom smoother settles, then enters the neutral fast
  path permanently (measured, Section 9).

## 12. Evidence

- Suite: `Tests/Source/C4/C4Phase5TuningTests.cpp` (registered in
  `APEXTests.jucer`, C4_CPP19) and `Tests/Source/C4/C4RenderPackTests.cpp`
  (C4_CPP20).
- Manifests: `evidence/runs/APEX.C4/` (Debug `230cb5c882e5`, Release
  `dc511cb40a80`), `evidence/runs/runner-smoke/` (Debug `7658c94d3938`,
  Release `d365fd640f5b`).
- Render pack: `evidence/renderpacks/C4_PHASE5/` (Section 8).
- Profile source: `Source/C4Core/C4TuningProfile.h::makeProfileProduction`.
