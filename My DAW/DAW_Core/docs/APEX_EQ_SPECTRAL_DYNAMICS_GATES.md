# APEX Native EQ — Spectral Dynamics DSP prototype

Implementation branch: `feature/apex-eq-spectral-dynamics-v1`
Parent: `feature/apex-eq-spectrum-grab-20261012` (PR #88).
Status: **DSP core and deterministic test cases only — NOT wired to the
production audio callback and NOT a shippable, user-selectable feature.**

## Why this is a separate gate

The public FabFilter Pro-Q 4 manual describes spectral dynamics as triggering
specific FFT frequencies *within* an EQ band's region rather than changing
the gain of an entire filter band. Density controls selectivity and a 3 dB/oct
spectrum tilt may affect detection. Exact implementation details are
proprietary; APEX has independently defined its own algorithms.

Reference: https://www.fabfilter.com/help/pro-q/using/spectral-dynamics

This stage deliberately leaves the deployed minimum-phase and linear-phase
paths, all 348 hosted parameter IDs, save-state ABI, audio threading and
plugin UI unchanged. Adding a spectral path before solving host latency and
bypass/automation coherence would violate APEX's professional RT contract.

## Implemented C++17 prototype

File: `Source/ParametricEQCore/ParametricEQSpectralDynamicsCore.h`.

- Fully owned preallocated 1024-point radix-2 complex FFT with real-input
  Hermitian reconstruction.
- 256-sample hop, 75% sqrt-Hann overlap, 1/2 synthesis compensation.
- Fixed **1024-sample latency** (= 21.33 ms at 48 kHz, 23.22 ms at
  44.1 kHz), including the neutral/unmodified signal path.
- No thread locks, no dynamic allocation, no worker dispatch, no GUI
  access, no file I/O in `process`.
- Up to 24 independently enabled spectral bell / low-shelf / high-shelf
  regions; conservatively bounded log-frequency envelopes.
- Per-frequency instantaneous log-power detectors with independent
  attack/release envelopes driven by the APEX signed dynamic-range law.
- Density [0, 1] changes spectral selectivity; optional +3 dB/oct
  detector tilt is clamped to ±18 dB.
- Stereo, Left, Right, Mid, Side placement; Mid/Side transform uses
  native APEX half-sum/half-difference energy convention.
- Nonfinite sample and configuration guards with bounded output level.
- Existing non-spectral EQ behavior and session state are untouched.

## Added test gate

`Tests/Source/ParametricEQ/ParametricEQProcessorTests.cpp` contains:
- Unity/neutral reconstruction to 1e-4 with 1024-sample PDC;
- zero output before latency;
- block independence for 64/128/256/512 and 44.1/48/96k sample rates;
- selective 1 kHz reduction with unrelated 5 kHz essentially unchanged;
- finite output on NaN/Inf input.

A preexisting State suite's index-343 literal had become stale when the output
stage appended IDs 343..347 on the parent branch. It has now been updated
to pin all 348 stable IDs by name, without touching runtime parameters.

### MUST PASS before integration

1. Windows Debug/Release compile of prototype and full native EQ suites.
2. Stereo M/S and density/tilt/shelf-specific measured sweeps.
3. Variable block-size + 24-active-band CPU/real-time allocation gauntlet;
   performance must meet an acceptable 48 kHz/128 production budget.
4. Processor ownership, format/channel layout, silent clip and negative
   amplitude edge tests.
5. **Host PDC**: report +1024 samples when spectral runtime is enabled,
   in addition to other active lookahead/linear-phase latency.
6. Bypass/enable/disable **transition** must remain click-free and aligned
   with delayed dry/wet audio; no unexpectedly time-shifted recording.
7. Dynamic gain paths must not process the same spectral band twice
   (ordinary biquad dynamic gain and spectral-specific attenuation).
8. New per-band spectral parameters, density and tilt may only be appended
   after frozen 0..347 ID range, with old-project migration defaults OFF.
9. Graph editor and automation recording/playback must expose native APEX
   controls with >=44px touch targets and no UI/DSP thread coupling.
10. Real listening and ASIO hardware acceptance at 48kHz/128 samples.

No exact FabFilter DSP equivalence or production readiness is claimed until
all gates are exercised and supported by results.
