# C4 Phase 1 — Permanent Parameter Contract (FROZEN)

Status: **FROZEN at Phase 1 closure** (2026-08-12). These 19 parameter IDs,
kinds, ranges, defaults and laws are the permanent APEX C4 compatibility ABI.
They must not change without an explicit versioning/migration reason (the
state schema is `kStateVersion = 1`; missing properties restore to defaults,
unknown fields are ignored, out-of-range values clamp, non-finite values keep
defaults).

## The 19 parameters

| # | Stable ID        | User-facing name | Kind          | Min        | Max        | Default   | Unit        | Law / mapping                        |
|---|------------------|------------------|---------------|------------|------------|-----------|-------------|--------------------------------------|
| 0 | `c4.weight.freq` | WEIGHT Freq      | BandFreq      | 30 Hz      | 450 Hz     | 100 Hz    | Hz          | LOG inside the band range            |
| 1 | `c4.weight.gain` | WEIGHT Gain      | BandGain      | -15 dB     | +15 dB     | 0 dB      | dB          | linear dB (continuous)               |
| 2 | `c4.weight.mode` | WEIGHT Mode      | Mode          | 0          | 1          | 0         | Bell/Shelf  | discrete 0/1                         |
| 3 | `c4.sculpt.freq` | SCULPT Freq      | BandFreq      | 120 Hz     | 2500 Hz    | 400 Hz    | Hz          | LOG inside the band range            |
| 4 | `c4.sculpt.gain` | SCULPT Gain      | BandGain      | -15 dB     | +15 dB     | 0 dB      | dB          | linear dB (continuous)               |
| 5 | `c4.sculpt.q`    | SCULPT Q         | BandQ         | 0.50       | 2.00       | 1.00      | Q           | LOG inside the user range            |
| 6 | `c4.bite.freq`   | BITE Freq        | BandFreq      | 400 Hz     | 9000 Hz    | 2500 Hz   | Hz          | LOG inside the band range            |
| 7 | `c4.bite.gain`   | BITE Gain        | BandGain      | -15 dB     | +15 dB     | 0 dB      | dB          | linear dB (continuous)               |
| 8 | `c4.bite.q`      | BITE Q           | BandQ         | 0.60       | 3.00       | 1.00      | Q           | LOG inside the user range            |
| 9 | `c4.open.freq`   | OPEN Freq        | BandFreq      | 1500 Hz    | 20000 Hz   | 10000 Hz  | Hz          | LOG inside the band range            |
| 10| `c4.open.gain`   | OPEN Gain        | BandGain      | -15 dB     | +15 dB     | 0 dB      | dB          | linear dB (continuous)               |
| 11| `c4.open.mode`   | OPEN Mode        | Mode          | 0          | 1          | 0         | Bell/HALO   | discrete 0/1 (text: Bell/HALO)       |
| 12| `c4.hpf`         | HPF              | Hpf           | 0=OFF      | 1          | 0         | Hz          | OFF=0.0; cutoffs (0,1] -> 20..1500 Hz |
| 13| `c4.lpf`         | LPF              | Lpf           | 0=OFF      | 1          | 0         | Hz          | OFF=0.0; cutoffs (0,1] -> 1.5..24 kHz |
| 14| `c4.bloom`       | BLOOM            | Bloom         | 0.0        | 10.0       | 4.0       | 0..10      | linear (continuous)                  |
| 15| `c4.input`       | Input            | Trim          | -18 dB     | +18 dB     | 0 dB      | dB          | linear dB (continuous)               |
| 16| `c4.output`      | Output           | Trim          | -18 dB     | +18 dB     | 0 dB      | dB          | linear dB (continuous)               |
| 17| `c4.autogain`    | Auto Gain        | Toggle        | 0          | 1          | 0         | On/Off      | discrete 0/1 (default OFF)           |
| 18| `c4.bypass`      | Bypass           | Toggle        | 0          | 1          | 0         | On/Off      | discrete 0/1                         |

## Phase 5 revision note (2026-08-13): BLOOM default 0.0 → 4.0

The `c4.bloom` DEFAULT was changed from 0.0 to **4.0** at Phase 5 closure
(the production sonic freeze — see `docs/C4_PHASE5_PERCEPTUAL_TUNING.md`).
The parameter ID, kind, range, and law are unchanged; state saved by older
builds still restores exactly (stored normalized values, not defaults).

BLOOM default 4.0 does NOT mean a flat instance is permanently nonlinear:

- The residual bank's activation is computed from the SMOOTHED band gain and
  is exactly 0 whenever every band is at 0 dB — no band gain, no color, at
  any BLOOM value.
- Once the BLOOM smoother settles, a flat instance re-enters the neutral
  fast path (`C4EngineCore::isSettledNeutral`, Phase 5 gate semantics:
  BLOOM is not a neutrality condition when every band gain is 0) and stays
  bit-identical to the wire.

Both behaviors are pinned by tests: `C4.NeutralPath` (neutral gate +
bit-identity) and `C4.Phase5Tuning` (production contracts).

## Laws

- **BandFreq**: `norm = ln(hz/min) / ln(max/min)` (clamped to [min,max]);
  `hz = min * (max/min)^norm` (norm clamped to [0,1]). Round trips exact to
  text precision; the float round trip may land a hair below 1000 Hz and
  format as "1000 Hz" (the unit comes from the text, never from the request).
- **BandQ**: same LOG law inside the user Q range.
- **BandGain / Trim / Bloom**: linear in the unit (dB / 0..10).
- **Mode / Toggle**: 0/1; text "On/Off", "Bell/Shelf", "Bell/HALO".
- **Hpf / Lpf** (Phase 1 corrected mapping — see below):
  `hz = 0` iff norm == 0.0 (OFF sentinel). Real cutoffs map the range
  [kHpfMinHz, kHpfMaxHz] = [20, 1500] Hz (HPF) and [kLpfMinHz, kLpfMaxHz] =
  [1500, 24000] Hz (LPF) onto the normalized interval **[normFloor, 1]** with
  `normFloor = 1/256`:
  - `norm = normFloor + t*(1 - normFloor)`, `t = ln(hz/min)/ln(max/min)`
  - `hz = min * (max/min)^t'`, `t' = clamp((norm - normFloor)/(1 - normFloor))`
  - OFF round-trips exactly: `hzFromNorm(0) == 0`, `normFromHz(0) == 0`
  - The minimum cutoff (20 Hz HPF / 1.5 kHz LPF) maps STRICTLY inside (0,1)
    and can never collide with the OFF sentinel (measured defect closed at
    Phase 1 closure).

## Automation behavior

All 19 parameters derive from `juce::AudioProcessorParameterWithID` and are
bound by stable string ID through `PluginChainCore::configureSlotAutomation`
(the C4.FormatHost automation-binding test proves every parameter carries its
stable ID). Plain-float value storage: control thread writes `setValue()`,
audio thread reads `getValue()`/`getUnitsValue()` — no locks, no allocation.

## Serialization behavior

ValueTree, schema version 1 (`kStateVersion`). Tolerant restore: missing
properties keep defaults, unknown fields ignored, out-of-range values clamped,
non-finite values keep defaults. Text/value conversions round-trip for every
kind (verified by C4.Processor text round-trip tests). State save/reload and
processor recreation preserve every parameter (C4.Processor state tests;
state save/reload at neutral stays bit-identical — C4.NeutralPath).

## OFF-state encoding (HPF/LPF)

- `c4.hpf` / `c4.lpf` at exactly 0.0 == OFF; text "Off"; round-trips to 0.0.
- Real cutoffs are strictly positive norms (floor 1/256) — no collision.
- The ENGINE clamps cutoffs to `0.49 * sampleRate` (Phase 1 TEMPORARY
  high-frequency clamp; the LPF 24 kHz control at 44.1 kHz behaves as
  ~21.6 kHz and is near-flat in-band — pinned by C4.Filters as documented
  TEMPORARY behavior). Phase 2's C4HaloShelfMapper replaces this clamp for
  the intentional perceptual OPEN/HALO mapping; the parameter IDs, ranges,
  defaults and laws above remain unchanged.
