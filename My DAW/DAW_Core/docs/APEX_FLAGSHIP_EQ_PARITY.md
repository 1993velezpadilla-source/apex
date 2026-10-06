# APEX Flagship EQ — Pro-Q-4-class parity plan

Status: implementation branch `feature/apex-flagship-eq-v1`

## Product rule

APEX may reproduce professional EQ workflows and independently implement equivalent
DSP capabilities, but it must not copy third-party source code, proprietary assets,
branding, preset content, or visual trade dress. The plugin remains an APEX-native
product with stable APEX parameter IDs and APEX UI language.

## Already present on main

- 24 fixed/stable band slots.
- Bell, shelves, low/high cuts, notch, band-pass, tilt, flat tilt and all-pass.
- Continuous/fractional cut slopes through adjacent-order interpolation.
- Stereo, Left, Right, Mid and Side placement.
- Per-band Dynamic EQ with threshold/range/attack/release.
- Peak/RMS detection, linked/unlinked stereo and external detector-key plumbing.
- Pre/post spectrum analysis and transient band audition.
- Minimum-phase realtime path, analog-matched design and Linear Phase path.
- Responsive/touch-safe APEX editor and intrinsic APEX Native hosting.
- Extensive state/automation/RT-allocation/sample-rate/editor regression suites.

## Phase 7/8 — in this branch

- [x] APEX Character stage: Pure / Velvet / Heat.
- [x] Character parameter appended at index 318 without moving IDs 0..317.
- [x] Character state persistence and host automation surface.
- [x] Character control in the responsive APEX top bar.
- [x] Correct Linear Phase latency reporting at prepare time.
- [x] Character regression tests and project registration.
- [x] EQ Sketch freehand planner + touch workflow + editor regression path.
- [x] Per-band Dynamic EQ sidechain filtering centered on band frequency/Q.
- [x] Repair Dynamic EQ/placement inspector parenting so controls live in the scroll viewport.
- [x] Extend append-only parameter ABI through index 342.
- [ ] Full Windows Debug + Release build and APEXTests gauntlet.

## Remaining flagship parity work

- Spectral dynamics: frequency-selective dynamic action inside a band's pass region.
- Dedicated sidechain-listen workflow (per-band detector filtering is now implemented).
- Spectrum Grab style peak suggestions.
- EQ Match capture, target curve generation and bounded band fitting.
- Cross-instance registry, collision visualization and native APEX track metadata.
- Output gain / auto gain, output spectrum controls and richer metering.
- Undo/redo and A/B state comparison at plugin scope.
- Copy/paste bands and preset exchange helpers.
- Piano-frequency overlay and expanded frequency labels.
- Surround/immersive channel placement once the APEX host exposes those layouts.
- Standalone VST3/CLAP/AU/AAX wrappers only if/when APEX product packaging requires
  external-host distribution; the intrinsic APEX Native path remains canonical.

## Realtime contract

All new audio processing must retain APEX invariants: fixed/preallocated storage in
`processBlock`, no locks, no allocation, no file I/O, no GUI work, no graph rebuild,
bounded transitions, finite-value guards, and truthful latency reporting.

## Gauntlet gates

1. Repository policy and evidence-format checks.
2. Parameter ABI/state migration tests.
3. DSP numerical safety and neutral-path tests.
4. RT allocation tests.
5. Block-size and sample-rate matrix.
6. Dynamic/sidechain/Mid-Side regression suites.
7. Linear Phase latency and kernel-transition tests.
8. Editor/touch-layout tests.
9. Full Debug and Release Windows builds.
10. Full APEXTests Debug and Release runs with seed `0xA9E12026`.
11. Manual audio + GUI validation before calling the feature production-ready.
