# C4 Phase 6 — Spectrum Flag (PASS)

Status: **C4 PHASE 6 — PASS** — declared 2026-08-13. Phase 5 production
tuning remains FROZEN and untouched by this phase. This document records the
Spectrum Flag architecture, the debugging forensics, and the regression
evidence.

## Architecture

`Source/C4Core/C4SpectrumCore.h` + `Source/C4Core/C4ResponseCurveCore.h`,
integrated into `C4Processor`:

```
audio thread: processBlock
    -> pushPre (top of block: mono-mix of the INPUT, PRE tap)
    -> [engine processing, bypass crossfade, neutral/bypass fast paths]
    -> pushPost (true final OUTPUT on EVERY path, POST tap)
        each: read-only copy into a preallocated lock-free SPSC ring
              (16384 floats, ~170 ms at 48 kHz; drop-newest on overload,
              never overwrites unread data, never blocks)

analyzer worker thread (low priority, "C4 Spectrum Analyzer")
    -> drains rings every 20 ms into staging buffers
    -> per full 4096-sample window: Hann-windowed radix-2 FFT
    -> per-stream one-pole smoothing (0.55/0.45) on dB magnitudes
    -> publishes PRE and/or POST magnitude spectra (2049 bins) in a
       double buffer by atomic snapshot index

GUI (Phase 7): polls getSnapshot() at 60 FPS — the FFT update rate is the
worker's (~11 Hz), never the frame rate.
```

- **No FFT in processBlock.** The audio callback performs exactly: PRE tap
  (mono-mix + ring copy), the existing engine math, POST tap (same cost).
  No heap, no locks, no GUI, no strings, no I/O — pinned by the zero-
  allocation tests in all three open modes.
- **Tap modes**: Closed (0, default), Pre (1), Post (2), Both (3). Stored as
  processor state (`c4.spectrum`, clamped 0..3, tolerant restore) — NOT a
  hosted parameter; the 19-parameter ABI stays frozen.
- **Closed = dormant**: no worker thread, no taps, no FIFO traffic, no FFT;
  the snapshot index invalidates on close and on prepareToPlay (no stale
  spectra across rate changes or reopen).

## PRE / POST / BOTH semantics

- PRE = the audio spectrum of the input as delivered to C4.
- POST = the audio spectrum of C4's true final output (post engine, post
  bypass crossfade — including the neutral and bypass fast paths).
- BOTH = both spectra. The measured response (post − pre) is available as a
  diagnostic; it is source- and window-dependent by nature.

## Total response curve

`C4ResponseCurveCore.h` derives the TOTAL C4 RESPONSE CURVE from the
engine's current smoothed state — band freq/gain/Q/mode, HPF/LPF (bilinear-
prewarped magnitudes, crossfade contract), trims, and the coupling contours —
"what C4 is mathematically applying". It is exact at every band center,
needs no audio, and is used by tests and the GUI. **Scope: linear only.**
BLOOM's nonlinear residual is deliberately NOT faked as a linear transfer
curve (a future BLOOM visualization is a separate decision).

## Bug forensics (the +2.03 dB failure)

MEASURED FACT (instrumented run): in BOTH mode, every FFT bin read a
uniform +1.74..+2.03 dB difference instead of +6 dB; PRE/POST shapes were
identical otherwise; zero FIFO drops at real-time cadence; PRE-only and
POST-only modes read correctly (−6.6 dB and −0.7 dB respectively).

ROOT CAUSE: `C4SpectrumCore` used ONE shared `smoothed_` array for both
streams. In BOTH mode the one-pole alternated between the PRE and POST
histories, coupling them; the steady-state published difference is
`0.29 * (d_post − d_pre)` for the 0.55/0.45 pole (solved exactly:
`0.2904 * 6.0 = 1.74 dB` — matching the measurement). A real analyzer
architecture defect, not a DSP or tuning issue.

EXACT FIX: per-stream smoothing state (`preSmoothed_` / `postSmoothed_`,
independent init flags) passed into `computeSpectrum()`. No assertion was
weakened; no production constant changed.

Secondary hardening (same investigation):
- The BOTH measurement now uses a tight region estimator
  (`maxDbNear(post) − maxDbNear(pre)`, f/1.4..f*1.4) instead of one shared
  bin: independently windowed PRE/POST frames can place Hann-window energy
  on different bins, so a single shared bin is not a robust transfer
  estimate. Documented lesson: a spectrum-transfer measurement from one FFT
  bin can produce false gain readings; separate signal spectra from the
  analytic filter response where possible.
- Tests feed at real-time cadence with a zero-drop assertion (the rings are
  designed to drop-newest under CPU-speed burst overload).
- The snapshot index resets on close/prepare so stale spectra are never
  presented.

## Regression evidence (all green)

| Gate | Result |
|---|---|
| C4.Spectrum (13 tests) | GREEN — measured: PRE −6.6 dB; POST +6 = −0.7 dB; BOTH +5.98 dB; BOTH cut −6.00 dB; analytic +6.00/−6.00/−18.13 dB; zero drops; symmetric consumption |
| APEX.C4 Debug | GREEN — manifest `a4046dbd9e33` |
| APEX.C4 Release | GREEN — manifest `571914df7b69` |
| Global Debug | GREEN — manifest `baa46e4f9372` |
| Global Release | GREEN — manifest `89488363e3cf` |
| Repository policy | PASS (exit 0) |
| Dependency verification | PASS (exit 0) |

Tests: `Tests/Source/C4/C4SpectrumTests.cpp` (C4_CPP21): disabled/dormant
path, PRE, POST, BOTH, BOTH cut, analytic curve, open/close lifecycle,
output bit-identity, zero-alloc callback, state persistence, sample-rate
change, block-size changes, multiple instances, reopen cycles.

## DSP-frozen contract

No C4 production constant, filter law, HALO mapping, coupling value, BLOOM
residual setting, default, or oversampling factor was changed during
Phase 6. The Spectrum Flag is observational only: no draggable nodes, no
spectrum grab, no auto-EQ, no parametric editing, no G10 three-bell system.
