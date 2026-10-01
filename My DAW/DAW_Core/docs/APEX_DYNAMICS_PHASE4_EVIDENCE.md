# APEX Dynamics Core Phase 4 — Reusable Dynamics Foundation (PASS)

Status: **PHASE 4 = PASS** — declared 2026-08-14 as a diagnostic working-tree
milestone. The shared workspace remains dirty and has no clean release
commit/tag; this is not release-grade evidence.

## Scope

Phase 4 builds the product-independent dynamics foundation shared by the
APEX Parametric EQ Dynamic EQ (Phase 5) and the future native APEX
Compressor. The core contains no plugin identity, no parameter ABI, no GUI,
and no APEX-specific tuning; host routing, parameter exposure, and state
persistence remain higher-level responsibilities.

## Architecture

`Source/DynamicsCore/ApexDynamicsCore.h` (header-only, fixed storage,
allocation-free, realtime-safe):

- **Detector** — peak and one-pole RMS level estimation (dB, 0 dBFS
  reference), stereo processing with linked (louder-channel) or averaged
  unlinked references, per-channel `processMono` for independent operation.
  The one-pole RMS window is ~10 ms (O(1) per sample; exact BS.1770 gating
  is a metering concern, not this detector's contract).
- **GainComputer** — the static downward compression law
  `over * (1 - 1/ratio)` with a quadratic soft knee (continuous value across
  both knee edges; the hard knee is the knee→0 limit) and an optional
  maximum-reduction range clamp. Boundary continuity is exact.
- **Envelope** — per-sample attack/release one-pole ballistics with the
  exact snap-to-target stagnation guard (when `next == state` no
  representable progress remains, the state becomes exactly the target).
- **DynamicsProcessor** — detector → gain computer → envelope → linear gain
  composition, plus **sidechain-ready overloads** (`processMonoFromKey`,
  `processLinkedGainFromKey`) that consume an explicit detector key signal;
  the core never owns sidechain plumbing.
- **Sanitisation** — `DynamicsParameters::sanitised` is total for NaN/Inf,
  out-of-range ratios, negative knees/ranges, and extreme time constants.

## Laws validated

- Static transfer: below threshold exactly 0; at threshold 0; over by X at
  ratio R yields exactly `X*(1-1/R)`; ratio 1.0 is unity everywhere; ratio
  100 approaches the limiter law (39.6 dB for 40 dB over).
- Soft knee: continuous in value at both edges (1e-12), monotonic bounded
  steps, hard-knee limit exact.
- Range clamp: in-range reductions unaffected, ceiling exact.
- Detector: peak exact on amplitudes; RMS settles to `A/sqrt(2)` within
  0.1 dB at all six rates; linked stereo reports exactly the louder channel.
- Envelope: attack reaches the target exactly; release decays below 1e-6 dB
  within 20 time constants; the one-pole per-sample ratio matches
  `exp(-1/(rate*tau))` at 44.1/48/88.2/96/176.4/192 kHz; extreme timings
  (0.5 ms attack, 20 s release) stay total.
- Processor: unity below threshold (1e-12), settled reduction for full-scale
  (15 dB at threshold -20, ratio 4, within 0.05 dB), linked stereo matches
  the louder channel's mono gain bitwise (1e-9).
- Sidechain-ready: key-driven overloads produce identical gain laws from the
  detector key; linked stereo key uses the louder key channel.

## Realtime / determinism

- Zero allocations under `UnitTestAllocationChecker` across Peak and RMS
  modes, mono/linked/keyed paths (20,000 iterations).
- Sample-exact block-size invariance (1 / 31 / 512) and exact
  reset/reprepare determinism.
- Silence stays finite and returns to unity (20τ bound); full-scale bursts
  stay bounded in (0, 1]; hostile parameter sweeps and extreme inputs
  (±1e30, denormals, ±Inf, NaN) produce finite non-negative gains.
- No locks, waits, filesystem access, GUI calls, or container growth exist
  in any processing path.

## CPU characterization (diagnostic, not a gate)

- Debug: ~149.5 ns per stereo-linked sample (4M samples).
- Release: ~23.5 ns per stereo-linked sample (4M samples).
- Evidence-only; recorded without optimization pressure.

## Lookahead architecture notes

- Lookahead belongs to the consumers that require it (Phase 5/Compressor),
  not to this core. The core remains zero-latency and causal.
- Delay buffers, latency reporting, and bypass/PDC coherence are owned by
  the host/processor layer; the detector/gain/envelope laws here are
  product-independent and unchanged by lookahead.

## Bugs found and fixed

- Three initial test failures were oracle/harness errors, not DSP: the knee
  endpoint expectation (the law gives 4.5 dB, not 6 dB, for 6 dB over at
  ratio 4), the release-to-zero window (asymptotic decay needs the 20τ bound,
  not a finite-window exact zero), and the attack-vs-release timing
  comparison (the release envelope was never pre-charged to its settled
  target). The silence-recovery window was likewise extended to the
  mathematically justified 20τ bound.

## Validation

All commands from `My DAW/DAW_Core`, seed `0xA9E12026`.

| Gate | Result |
|------|--------|
| Focused Debug `APEX.Dynamics` | PASS — 16 groups, 342,918 assertions, 0 failed |
| Focused Release `APEX.Dynamics` | PASS — 16 groups, 342,918 assertions, 0 failed |
| Complete repository Debug | PASS — 711 groups, 558,193 assertions, 0 failed |
| Complete repository Release | PASS — 711 groups, 558,193 assertions, 0 failed |
| Debug application rebuild | PASS |
| Release application rebuild | PASS |
| `test_repository_policy.ps1` | PASS |
| `test_validate_test_evidence.ps1` | PASS — 6 passed, 0 failed |
| `verify_dependencies.ps1` | PASS (run inside both builds) |
| Phase 4 source hygiene + diff check | PASS |

## Evidence run identifiers

| Scope | Configuration | Run ID | Exit |
|-------|---------------|--------|------|
| Focused `APEX.Dynamics` | Debug | `5f8ac27066ac` | 0 |
| Focused `APEX.Dynamics` | Release | `d6459f578c21` | 0 |
| Complete repository | Debug | `20b17f617954` | 0 |
| Complete repository | Release | `d52d89c3b2b7` | 0 |

## Artifact inventory (UTC, SHA-256)

| Artifact | Bytes | Timestamp | SHA-256 |
|----------|------:|-----------|---------|
| Debug `DAW_Core.exe` | 39,782,912 | 2026-08-14 22:40:13 | `4C9C13388D24522512466F4ABA9AD9A6EA2B497CFB0BF6A2D6AB0DC662EF511B` |
| Release `DAW_Core.exe` | 13,334,016 | 2026-08-14 22:50:17 | `5E68E07BADA590DE0A4B58CC81FFC44D564AE76EF05BBB3F29FA432C722D6AF5` |
| Debug `APEXTests.exe` | 27,447,296 | 2026-08-14 21:57:54 | `8BEE830EFF019963D663E6AF7B42088B33CD7A17AF19548F2CB362D4C84EC2DB` |
| Release `APEXTests.exe` | 9,598,464 | 2026-08-14 22:00:51 | `DFF17193653D78662E31197E02AB667CA43EB2EA037CE90E9F9067F188315E3D` |
| Focused Debug results | 2,463 | 2026-08-14 21:57:58 | `EAB4367864B0B0E1F72ACC8A8AA7C52E672432BDE09548D52BE7E72B4C8A8571` |
| Focused Release results | 2,465 | 2026-08-14 22:00:54 | `8961C1DD87FAC106F689417BA5DB44A61A0B5A042AC4BA21AC3CCB01072539E1` |
| Full Debug results | 104,447 | 2026-08-14 22:26:38 | `1657FDFA0248FA617D077ED34614B5AFA7A75B9DFA67601268EB6F9768DC9A9D` |
| Full Release results | 104,449 | 2026-08-14 22:33:34 | `CA72E85D4D39180125363BAC4711B6BF22C19A43A126990499770B876FC4F17E` |

## Limitations

- One-pole RMS is an O(1) detector approximation; exact standardized
  loudness gating is out of scope for this core.
- No lookahead, detector sidechain filtering, or host routing is implemented
  here (consumer responsibilities).
- Evidence is diagnostic grade; a clean release commit/tag remains a later
  release gate.
