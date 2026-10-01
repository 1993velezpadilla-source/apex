# APEX G10 Plugin Postmortem

| Field | Value |
|-------|-------|
| Document | Permanent engineering memory (historical record) |
| Case ID | APEX-G10-POSTMORTEM-001 |
| Status | Final record; corrections append, never rewrite history |
| Last verified | 2026-08-11 (final architecture chapter appended) |
| Source lineage | `feature/g10-vst3` at `a838ebe43af3f24526135fd4da3180f564c80f25` |
| Companion doc | `docs/APEX_PLUGIN_ENGINEERING_PLAYBOOK.md` |

## 1. Executive verdict

APEX G10 — a 10-band graphic equalizer with an always-on analog character
chain — went from concept to a frozen, published, user-validated Windows VST3
in one integrated effort. The DSP path, state schema, and 15-parameter Native
contract were frozen at `g10-canonical-freeze`; the external Windows VST3
milestone (13 public parameters, HostSmoke, GUI/analyzer finalization) is
frozen at `g10-windows-vst3-1.0`.

The most valuable engineering outcomes are not the plugin itself but the
debugging discipline it forced:

- where heap corruption is *detected* is not where it *started*;
- validate the *test* before retuning proven sound;
- analyzer freshness is a throughput contract, not a style choice;
- hide legacy state, never delete it.

## 2. Scope and non-scope

**Scope.** The full G10 story: musical concept, DSP architecture, GUI and
analyzer development, VST3 conversion, HostSmoke, validation/freeze/
publication, and every documented bug/ghost-bug.

**Non-scope.** The pre-existing Native `uniqueId`/`deprecatedUid`
chain-restore defect (`PluginChainCore` identity mismatch) is tracked
separately and was deliberately not modified by this milestone. macOS work
was not started. No production code was changed to produce this document.

## 3. Authority and evidence-label policy

This document follows the DAW Brain v7.0.0-alpha.1 evidence taxonomy
(§28, §41, §42) and the repository's own authority order:
official contract → Brain interpretation → repository source → build
artifact → runtime result → tag annotation → living memory.

Evidence classes used inline:

| Class | Meaning |
|-------|---------|
| [GIT] | Immutable commit/tag/blob evidence |
| [CODE] | Current tracked source contract |
| [TS] | Committed test source — proves coverage exists, not that it passed |
| [ARTIFACT] | Retained binary/PDB/module/metadata (ignored or untracked) |
| [RUNTIME] | Run observed during the 2026-08-09 validation session (raw stdout not archived in repo) |
| [TAG] | Claim recorded in an annotated tag annotation |
| [USER] | Supplied by the user/author; not independently retained |
| [UNAVAILABLE] | Expected evidence that was not found |

Rules applied:

- A tag annotation records a claim; it is not a fresh independent rerun.
- Repository `diagnostic`/`release` evidence grades are separate from these classes.
- Failure episodes that predate the first G10 commit are marked [USER] — the
  repository contains no intermediate "bug → fix" commits; the entire G10
  subsystem landed atomically in `d0be566` with fixes already embedded.

## 4. Source, branch, and tag timeline

| Date (UTC) | Event | Evidence |
|------------|-------|----------|
| 2026-08-08 20:31 -04:00 | `d0be566` — entire G10 subsystem lands atomically: core, editor, analyzer, tests, fixes embedded (11,753 insertions) | [GIT] |
| 2026-08-08 20:33 -04:00 | `e4176c2` — integrate G10 into APEX application target | [GIT] |
| 2026-08-08 20:36 -04:00 | Annotated tag `g10-canonical-freeze` → `e4176c2` (tag object `63296f593343e16df9a684522313014fab331dbf`, unsigned) | [GIT]/[TAG] |
| 2026-08-09 12:09 -04:00 | `a838ebe` — external VST3 target, HostSmoke, GUI/analyzer finalization, parameter exposure; DSP `processBlock` unchanged from freeze | [GIT] |
| 2026-08-09 12:10 -04:00 | Annotated tag `g10-windows-vst3-1.0` → `a838ebe` (tag object `99340017ba0523e9daf1f99a286a91eab4441739`, unsigned) | [GIT]/[TAG] |
| 2026-08-09 | Final Release module built 14:53 UTC; HostSmoke executable built 14:55 UTC | [ARTIFACT] |
| 2026-08-09 | Validation session: default HostSmoke, real-editor review, full 16, repeat 8×32, published-path smoke all exit 0 with `failures=0` | [RUNTIME] |
| 2026-08-09 | Transactional publication to `Apex plug-ins\Windows\APEX G10.vst3`; published hash verified equal to accepted internal hash | [RUNTIME]/[ARTIFACT] |
| After publication | User manually confirmed APEX G10 opens as a VST3 in a real third-party DAW | [USER] |

**Freeze vs external milestone.** `g10-canonical-freeze` is the integrated
Native/canonical G10 baseline (no `Plugins/G10VST3` exists there). The
external Windows VST3 product appears one commit later at
`g10-windows-vst3-1.0`. They must not be collapsed into one freeze event.

## 5. Product concept and architecture

### 5.1 Musical concept and band architecture

Ten musical bands with permanent names and research lineage (Pultec/SSL/Neve/
Harrison/API/Trident/Maag philosophies) — `Source/G10Core/G10Types.h:58-101`,
`G10CurveFamily` at 77-89, 15-slot parameter layout at 107-125.

| Band | Family design (G10CurveEngineCore.h:398-484) |
|------|----------------------------------------------|
| DEEP (31) | Compound bell + shelf at 45 Hz |
| PUNCH (63) | Bell + 120 Hz support at 0.05 weight |
| BODY (125) | Hybrid 70/30 vs 83/17 |
| WARMTH (250) ... FOCUS (1k) ... | Distinct bell/Q-law designs per family |
| SHINE (8k) | Continuous bell↔shelf morph |
| AIR (16k) | Exact RBJ high-shelf + 9 kHz support |

Family distinctness is a committed contract: `G10EngineTests.cpp::testCurveFamilies` (504-773).

### 5.2 Signal flow

```text
Input Trim
  → Oversampling (2x realtime / 4x offline)
  → D1B (G10DiscreteInputStageCore: discrete input stage, tanh residual + even asymmetry)
  → clean curve engine at OS rate (TPT SVF + RBJ shelves)
  → I1 (G10IronOutputStageCore: flux-modulated saturator)
  → Downsample (AA + decimate)
  → Output Trim
  → bypass crossfade
```

- Nominal level tie: −1.1 dB per stage at 0 dBFS (`G10AnalogEngineCore.h:41-46`).
- Realtime 2x / offline 4x: `qualityMix_` snapped from `isNonRealtime()` at
  `prepareToPlay` so the start state is click-free by construction
  (`G10Processor.h:247-284`).
- Canonical Internal Color: the analog path is **always** active
  (`analogTarget` hardcoded `true`, `G10Processor.h:307`). The legacy
  `g10.analog`/`g10.quality` parameters are serialized but DSP-inert.

### 5.3 Clean path (dormant regression authority)

`engine_` is the "frozen Phase 1 clean path (regression authority)"
(`G10Processor.h:609`). It is **not** in the canonical audio path; tests must
never inspect it for canonical behavior (see G10-15).

### 5.4 State schema

`ValueTree("g10state")`, `version=1`, 15 parameter properties; tolerant
restore (unknown ignored, missing keep defaults, out-of-range clamped,
non-finite skipped) — `G10Processor.h:480-525`, `kStateVersion=1`
(`G10Types.h:44`). Coverage: `G10ProcessorTests.cpp::testState` (264-401).

### 5.5 Parameter architecture

- Native: 15 hosted parameters (exact ID order, `G10ProcessorTests.cpp:85-160`).
- External VST3: 13 public parameters; `g10.analog`/`g10.quality` remain
  owned state-only objects, not host-exposed (see G10-32).
- `getBypassParameter()` returns the existing `g10.bypass` (see G10-29).

### 5.6 Realtime safety

- Zero allocation in the audio path: preallocated `scratchA_/scratchB_`,
  SPSC analyzer FIFO (fixed 16,384 capacity, lock-free), denormal flush
  (`< 1.0e-30f`) on filter states and final samples.
- Oversized blocks are chunked at the processor boundary
  (`G10Processor.h:346-363`) — see G10-28.
- Allocation audits scope strictly to the audio-thread contract
  (`G10RtAllocationTests.cpp`) — see G10-12.

## 6. Bug / ghost-bug database

Index:

| # | Area | One-line symptom | Fix in one line |
|---|------|------------------|-----------------|
| 1 | DSP | Smoothing advanced at wrong cadence under oversampling | Prepare engine at the OS rate; coefficient derived from the rate the function actually runs at |
| 2 | DSP | Per-channel filter-state problems | Coefficients shared, every filter delay state per channel |
| 3 | DSP | TPT normalization | `band = v1 * k_` (k_ = 1/Q) so bell reads as fader gain at anchor |
| 4 | DSP | AIR shelf not an exact shelf | Exact RBJ TDF2 shelf + gain-dependent shelf Q |
| 5 | DSP | Analog quality transition wrong | Analog always on; quality from host `isNonRealtime()` snapped at prepare |
| 6 | DSP | NORMAL→HQ serial cascade | Parallel processing of the same dry input, per-sample crossfade |
| 7 | DSP | Zero-stuff ×L missing (−6 dB / −12 dB) | `× factor` normalization after interpolation, before nonlinear stages |
| 8 | DSP | H5/H9/H15 misread as aliases | Alias-only metric excluding real harmonics and their mirror bins |
| 9 | DSP | D1B DC | One-pole 2 Hz DC blocker on the residual only; linear path untouched |
| 10 | DSP | Iron foldback risk | Flux-modulated strength capped at 0.95; static transfer monotonic to x=8 |
| 11 | DSP | Candidate vs production divergence | Tests drive only the real production path; legacy harnesses quarantined |
| 12 | Test | RtAllocation false positives | Checker scoped strictly around `processBlock`; host text machinery excluded |
| 13 | Test | Stale test executable | Rebuild before every run; binary hash + commit + dirty flags in manifest |
| 14 | Test | State carryover false bit-exact failures | Fresh, identically-settled processors; document state time constant |
| 15 | Test | Tests read dormant clean engine | Explicit "do not inspect" contract; measure the canonical chain |
| 16 | Test | Impulse/static measurements invalid after nonlinearity | Level-matched measurement at operating level |
| 17 | Test | LF impulse FFT absurd (+31/+41 dB) | Steady-sine RMS with explicit settle/discard windows |
| 18 | GUI | No editor (hasEditor=false, pre-commit) | First committed snapshot already implements the editor |
| 19 | GUI | Headless lifecycle crash | Editor tests create a real JUCE GUI context |
| 20 | GUI | Faders invisible (editor coords in local components) | Translate to child-local coordinates at the component boundary |
| 21 | GUI | INPUT/OUTPUT text overlap | Non-overlapping geometry (body 16.5–53.5, label 0–11) |
| 22 | GUI | Bipolar knob arc wrong | One graphics-library angular convention everywhere |
| 23 | GUI | Double-click reset issue | Multi-click rejection + dragging_ guard + gesture close |
| 24 | GUI | No mouse wheel | Explicit wheel path: dominant axis, reversal, Shift fine, clamp |
| 25 | GUI | Eyes read as balls/gems/LEDs | `makePredatoryEye`: almond aura/lid, slit pupil; removed diamond rune |
| 26 | GUI | Analyzer = black rectangle/grid | Cached chamber, radial depth, band-aligned landmarks, restrained strokes |
| 27 | Analyzer | Startup latency ghost (1783.821 ms) | Newest-window freshness; discard stale history; visibility reset; clamped targets; 32.561 ms final |
| 28 | DSP | Oversized-block crash (scratch assumptions) | Chunk full blocks at processor boundary; bit-equivalent to legal blocks |
| 29 | VST3 | Duplicate Bypass | `getBypassParameter()` override |
| 30 | VST3 | HostSmoke destruction heap corruption | Declare max 4096; never submit beyond negotiated capacity |
| 31 | VST3 | Many-instance/destruction correlation | Isolate dimensions (audio/editor/params/destruction) before blaming count |
| 32 | VST3 | External Analog/Quality exposure | `ParameterExposure` policy: Native 15, external 13, legacy state-only |
| 33 | VST3 | External automation→GUI index mismatch | Hosted→semantic index mapping for Bypass (12→14) |
| 34 | DSP | Bypass crossfade state discipline | Chain-state freeze during bypass; convergence to never-bypassed reference |
| 35 | DSP | Reused-processor 16 kHz droop (−34.5 dB) | Phase cancellation clean-vs-chain at crossfade m≈0.785; snap quality at prepare |
| 36 | Test | Feedback-loop Inf growth | Numerical-safety test cleared input before every block |
| 37 | Test | responseFactor bias metric | Direct Q-law introspection replaced biased boost/cut ratio |

### DSP issues

#### G10-01 — Smoothing advanced at wrong cadence

- **Symptom** — parameter smoothing stepped at the wrong rate once the analog chain ran at the oversampled rate. [USER]
- **Root cause** — cadence mismatch between base-rate preparation and the oversampled sample count.
- **Fix** — the chain's internal engine is prepared at the OS rate (`engine_.prepare (osRate, osMax, numChannels_)`, `G10AnalogEngineCore.h:456`) so `bandSmoothCoeff_ = 1-exp(-1/(osRate*0.020))` (`G10CurveEngineCore.h:505`) matches once-per-OS-sample advance (594-595); musical time constant stays constant.
- **Proof** — `G10EngineTests.cpp::testSmoothing` (783-818): same 2.48 dB after 512 base-rate samples at both rates, <0.05 dB single-step, block-size-independent final state (869-889). [TS]/[CODE]
- **Lesson** — when a block function is called with `numSamples*factor`, the smoothing coefficient must be derived from the rate the function actually runs at.

#### G10-02 — Per-channel filter-state problems

- **Symptom** — channel state isolation broken (mono vs stereo divergence or L≠R). [USER]
- **Root cause** — state/coefficient ownership confusion.
- **Fix** — coefficients shared; every filter delay state per channel (`secA_[kMaxChannels]`, `secB_`, `support_`, `shelf_`, `G10CurveEngineCore.h:317-320`), D1B `dcState_[2]`, I1 `fluxRaw_[2]`, AA per channel.
- **Proof** — bit-exact mono == stereo-left == right, including mid-stream parameter change (`G10EngineTests.cpp:341-406`; `G10AnalogTests.cpp:2906-2969`, 408-447). [TS]/[CODE]
- **Lesson** — shared smoothing + per-channel filter state is the correct split; reset stateful stages between mono and stereo runs.

#### G10-03 — TPT normalization corrections

- **Symptom** — bell contribution did not read as the fader gain at center. [USER]
- **Root cause** — raw TPT band output peaks at `|band(f0)| = Q`.
- **Fix** — `band = v1 * k_;` with k_ = 1/Q (`G10CurveEngineCore.h:72-79`). Pure gain on the band output: no phase/minimum-phase/state change.
- **Proof** — center-gain bounds ±6 dB per family (`G10EngineTests.cpp:514-540`); direct Q-law introspection (`bandQAt`, 596-654). [TS]/[CODE]
- **Lesson** — the (A−1)·band bell mix requires the 1/Q normalization to equal the fader dB at center.

#### G10-04 — AIR shelf implementation issues

- **Symptom** — up to ~1.0 dB error at 8 kHz and a spurious ~1.2 dB scoop at 4 kHz; SVF highpass-mix shelf was not an exact RBJ shelf. [CODE comment]
- **Root cause** — TPT-derived shelf topology is not an exact RBJ shelf.
- **Fix** — exact RBJ high-shelf `G10ShelfBiquad` (TDF2) for Air16k; gain-dependent shelf Q `0.5*sqrt(10^(db/20))` ("Preserved production fix"); rate-adapted turnover one octave below anchor; 9 kHz support contour (`G10CurveEngineCore.h:93-99, 231-245, 358-364, 388-391, 461-463, 481-482`).
- **Proof** — locked AIR contract +6 dB ∈ [5.4, 6.6], −6 dB ∈ [−5.6, −4.0], no 4 kHz scoop, monotonic treble, cross-rate 44.1/96/192 kHz (`G10EngineTests.cpp:691-759`); near-Nyquist safety <12 dB at 44.1 kHz (491-498). [TS]/[CODE]
- **Lesson** — verify shelf topologies against the RBJ reference; the anchor-gain contract must hold across all sample rates.

#### G10-05 — Analog quality transition initially behaving incorrectly

- **Symptom** — quality path selection behaved incorrectly during transitions. [USER]
- **Root cause** — transition policy relied on legacy parameter state.
- **Fix** — analog always on (`analogTarget = true`, `G10Processor.h:307`); quality from host `isNonRealtime()` snapped at `prepareToPlay` (line 275); legacy values serialized but inert (304-308).
- **Proof** — legacy bit-inertness through real path incl. mid-stream toggles (`G10ProcessorTests.cpp:405-501`; `G10EngineTests.cpp:154-256`; `G10AnalogTests.cpp:759-798`; HostSmoke hosted DSP `Main.cpp:806-836`). [TS]/[CODE]/[GIT]
- **Lesson** — if a product decision makes a control inert, prove bit-inertness through the real audio path, including mid-stream toggles.

#### G10-06 — NORMAL→HQ serial transition vs correct parallel crossfade

- **Symptom** — the HQ chain received NORMAL's processed output during transition (serial cascade). [USER]
- **Root cause** — serial chaining during the quality ramp.
- **Fix** — both chains process the **same dry input** in parallel (scratchB_ holds the dry copy); `dst = NORMAL*(1-q) + HQ*q` per sample over ~10 ms. "HQ must NEVER receive NORMAL's processed output" (`G10Processor.h:406-428`).
- **Proof** — NORMAL→HQ settles bit-exact to the HQ endpoint; HQ→NORMAL vs state-equivalent reference <1e-6 residual, no click, <0.1 dB jump; block-size bit-exactness (`G10AnalogTests.cpp:1942-2030`); ramp ~10 ms (1807-1867). [TS]/[CODE]
- **Lesson** — crossfade between two nonlinear chains requires parallel dry routing; endpoint bit-exactness is the strongest routing assertion.

#### G10-07 — Zero-stuff oversampling missing ×L compensation

- **Symptom** — ~−6 dB at 2x and ~−12 dB at 4x level loss; nonlinear drive changed with quality mode. [USER (magnitudes)]
- **Root cause** — zero insertion alone scales the baseband by 1/L ("zero insertion alone scales by 1/L", `G10AnalogEngineCore.h:174`).
- **Fix** — `dst[i] = aa_[ch].process (dst[i]) * (float) factor_;` in `processUp` (line 244); the ×L restores the reconstructed baseband to original amplitude so nonlinear stages see the same level regardless of factor (169-176, 223-227). Downsampler is AA+decimate, no gain change (250-266).
- **Proof** — unity (0 dB) linear round-trip for both factors (`G10AnalogTests.cpp:2093-2149`); stage-by-stage gain trace: zero-stuff-only = 1/L, after ×L = input (1463-1573); alias tests at −6 dBFS validate comparable drive across 1x/2x/4x (546-680). [TS]/[CODE]
- **Lesson** — any zero-stuffed oversampler needs ×L gain normalization before nonlinear stages, and the linear round trip must be a committed unity-gain regression.

#### G10-08 — Oversampling alias analysis; misleading H5/H9/H15 interpretations

- **Symptom** — alias conclusions were drawn from harmonic bins (H5/H9/H15) that are legitimate saturation products, not aliases. [USER]
- **Fix** — alias-only metric excluding real harmonics **and their conjugate mirrors** (`rate - n*f`), because a real harmonic appears at both +n·f and its mirror (`measureDistortion`, `G10AnalogTests.cpp:128-149`). Contract note: 20 kHz residual dominated by up-AA image intermodulation, not H9/H15 wraps (~−140 dB) (560-585). Factor-specific AA: 2x = 8th-order/1.1x Nyquist; 4x = 10th-order/1.0208333x (24.5 kHz) to restore 4x ≥ 2x despite tan-warp (`G10AnalogEngineCore.h:53-64, 194-212`).
- **Proof** — 1x strong aliasing (>−45 dB); 2x/4x floor <−30 dB at 5/10/20 kHz; 2x beats 1x ≥10 dB at 10 kHz; 4x not worse than 2x at 10 kHz (−59.5 vs −57.5 dB); 4x improves at 20 kHz (−45.0 vs −40.8 dB) (`G10AnalogTests.cpp:546-680`); AA coefficient contract (1229-1296); AA isolation of zero-stuff images (1298-1381). [TS]/[CODE]
- **Lesson** — harmonic bins are not aliases; mirror bins are not independent; measure alias floors with real-harmonic exclusion.

#### G10-09 — D1B DC issue and residual-only 2 Hz DC blocker

- **Symptom** — DC produced by the even asymmetry residual. [USER]
- **Fix** — Phase 2B accepted D1B topology: `residual = (tanh(x)-x)*kSaturation + kAsymmetry*x*tanh(x)`; one-pole LP at 2 Hz on the **residual only** — the linear path is exactly x, so there is no low-cut on audio (fundamental gain 0.000 dB at every frequency) while residual DC is removed (≥26 dB at 40 Hz, ≥60 dB at 1 kHz) (`G10AnalogEngineCore.h:277-349`; `kDcBlockerHz = 2.0f` at 304).
- **Proof** — DC measured −95.1 dBc at 0 dBFS, asserted <−60 dBc (`G10AnalogTests.cpp:364-370`); settled DC transfer exactly linear y = x (305-332); DC suite incl. asymmetric transients and silence-after-drive tail <1e-5 within 0.75 s (2692-2754); bounded stateful impulse tail (2430-2459). [TS]/[CODE]
- **Lesson** — block DC on the nonlinear residual, never on the linear path; statefulness must be bounded and asserted.

#### G10-10 — Iron monotonicity / foldback tuning

- **Symptom** — foldback risk in the flux-modulated saturator. [USER]
- **Fix** — I1: flux one-pole at 60 Hz, `flux = tanh(fluxRaw)`, `strength = kSaturation*(1 + kFluxDepth*|flux|)` with `kFluxDepth = 0.9` capping strength at 0.95 — static transfer monotonic for every input, no foldback even at +18 dB trim; flux modulates strength only, the linear term is x (`G10AnalogEngineCore.h:352-423`).
- **Proof** — "No foldback: static DC transfer must be monotonic up to x = 8 (covers +18 dB trim)" (`G10AnalogTests.cpp:3071-3082`); flux charge/decay statefulness (2472-2488); monotonic THD-vs-level (486-497); block invariance (2509-2517). [TS]/[CODE]
- **Lesson** — cap modulated strength so the static map stays monotonic over the whole trim range; test to x=8, not just ±1.

#### G10-11 — Candidate DSP vs production bit-exact validation

- **Symptom** — candidate/harness DSP diverged from the production path. [USER]
- **Fix** — all measurements go through the real production code ("All measurements go through the real production code", `G10AnalogTests.cpp:11-19`); legacy harnesses quarantined in `Tests\Legacy` with an explicit non-evidence README and absent from `APEXTests.jucer`; the VST3 milestone validates the actual published binary via HostSmoke.
- **Proof** — `APEXTests.jucer` compiles only `Tests\Source\G10` + production `G10Processor.cpp`; `G10VST3Entry.cpp` returns `G10Processor` directly; product README: "Do not add a delegating processor or duplicate the G10 source tree". [GIT]/[CODE]/[TS]
- **Lesson** — a test harness is evidence only if it drives the exact production binary/path; quarantine anything else with an explicit non-evidence statement.

#### G10-28 — Oversized processor block crash (scratch-buffer assumptions)

- **Symptom** — a host delivering a block larger than the prepared maximum crashed the canonical analog path. [USER (reproduction)]
- **Root cause** — analog-chain scratch buffers (`osBuffer_`, `dryScratch_`) are sized `maxBlockSize_*factor` in `prepare` (`G10AnalogEngineCore.h:450, 468-469`); the chain's only guard is a Debug-only jassert (534-538).
- **Fix** — the processor chunks the complete host buffer at its ownership boundary: all DSP state lives in members and is preserved across chunk boundaries, so chunking is bit-equivalent to legal smaller blocks; no allocation, no locks, no resize (`G10Processor.h:346-363`; `processChunk` 370-446).
- **Proof** — `testOversizedBlocks`: sizes {513, 1024, 4096} vs prepared 512, both modes, mono/stereo, all finite, `maxErr < 1.0e-6` vs split-block reference (`G10EngineTests.cpp:260-337`); HostSmoke renders 513/1024/4096 through the real VST3 (Main.cpp:853-859). [TS]/[CODE]/[GIT]
- **Lesson** — hosts may exceed the prepared block size; chunk at the processor boundary so the chain never sees an illegal size, and prove bit-equivalence to legal blocks.

### Test-infrastructure issues

#### G10-12 — RtAllocation false positives caused by test-side work

- **Symptom** — the allocation checker flagged the processor for allocations made by test/host machinery. [USER]
- **Fix** — everything created/prepared before the checker; `juce::String` formatting and `getValueForText` explicitly classified as "HOST machinery (control-plane text conversion), NOT the audio-thread contract" and precomputed outside the measured scope — only the raw float store inside (`G10RtAllocationTests.cpp:9-31, 86-91, 173-240`).
- **Proof** — nine scenarios × block sizes {1,32,128,333,1024} incl. canonical realtime 2x and offline 4x (41-56, 259-277). [TS]/[CODE]
- **Lesson** — allocation audits must scope strictly to the audio-thread contract; move host text conversion out, with the rationale committed.

#### G10-13 — Stale test executable / build issue

- **Symptom** — results came from a stale test binary. [USER]
- **Fix** — `run_apex_tests.ps1` builds before every run and fails if `APEXTests.exe` is missing, then records `binarySha256`, outer/application commit SHAs and dirty flags; grade = 'release' only when both trees are clean; manifest validated against the evidence schema and published atomically. `G10NativePluginFormat::pluginNeedsRescanning() == false` — "intrinsic; never stale".
- **Proof** — script sections 75-90, 150-260; schema fixtures prove dirty/grade semantics are tested. [GIT]/[CODE]
- **Lesson** — binary hash + commit + dirty flags in a validated manifest make stale-binary results detectable by construction.

#### G10-14 — State carryover creating false bit-exact failures

- **Symptom** — bit-exact comparisons failed because processors reused internal state (D1B DC blocker, tau ≈ 79.6 ms). [USER]
- **Fix** — fresh-processor methodology: "the D1B DC blocker (2 Hz, tau ~79.6 ms) retains state across blocks, so reusing one processor between comparisons would compare different internal states" (`G10EngineTests.cpp:161-165`; also `G10AnalogTests.cpp:931-935`, `G10ProcessorTests.cpp:458-464`); HQ→NORMAL compared against a state-equivalent reference with identical history.
- **Proof** — fresh-instance bit-exactness (`G10ProcessorTests.cpp:465-493`); neutral-path B/C/D at six rates. [TS]/[CODE]
- **Lesson** — with long-tailed state, bit-exact assertions need fresh, identically-settled instances or state-equivalent references; document the state time constant in the test.

#### G10-15 — Tests reading the dormant clean engine instead of the canonical chain

- **Symptom** — tests inspected `G10Processor::getEngine()` (frozen but dormant clean engine) and concluded the wrong thing. [USER]
- **Fix** — explicit contract: "the processor's dormant clean engine is not in the canonical path and must not be inspected for this" (`G10EngineTests.cpp:790-791, 1004-1006`; `G10AnalogEngineCore.h:517-524`); curve-shape contracts measured on a bare curve engine because "processor-level impulse measurements can no longer isolate the EQ-curve response" (`G10TestUtils.h:323-329`).
- **Proof** — bypass crossfade duration verified on the chain (1004-1017); trim smoothing measured relative to an identical-reference processor (821-867). [TS]/[CODE]
- **Lesson** — name the dormant engine as such in code comments and route every behavioral contract to the actual audio path.

#### G10-16 — Nonlinear DSP invalidating impulse/static measurement assumptions

- **Symptom** — impulse-derived static gains did not predict steady-state response once D1B+I1 were level-dependent. [USER]
- **Fix** — level-matched measurement (`measureGainDbAtLevel`): an impulse (0 dBFS) is compressed differently than a steady sine; measuring at the operating level makes both quantities describe the same nonlinear operating condition (`G10TestUtils.h:283-321`).
- **Proof** — numerical-safety suite judges RMS against the static response at the same operating level (`G10EngineTests.cpp:1091-1128`); −40 dBFS linear-region response measured through the real processor (`G10AnalogFreqSafetyTests`, 686-722). [TS]/[CODE]
- **Lesson** — once saturation is in the chain, static-vs-dynamic consistency must be measured at the operating level; document why in the helper.

#### G10-17 — Low-frequency impulse FFT producing physically absurd curve results

- **Symptom** — "physically impossible readings (+31/+41 dB for a design whose Q laws cap the center gain near +6 dB)" — committed test comment (`G10EngineTests.cpp:96-103`). [TS numbers; specific measurements [USER]]
- **Root cause** — short impulse-FFT at 31/63 Hz is sensitive to window truncation, FFT-bin resolution, and leakage for compound low-frequency families (DEEP 31 Hz bell + 45 Hz shelf, PUNCH 63 Hz + 120 Hz support).
- **Fix** — `measureEngineGainDbSteady`: low-level steady sine with 2 s settle (31 Hz needs ~1.5 s), 1 s RMS measure, transient discarded (`G10TestUtils.h:368-432`); `bandGainAt` uses it exclusively (`G10EngineTests.cpp:104-111`).
- **Proof** — all ten-family contracts now measured by steady sine (504-773); the absurd numbers committed as a warning. [TS]/[CODE]
- **Lesson** — impulse-FFT at frequencies with few cycles in the window is invalid for compound LF designs; steady-sine RMS with explicit settle/discard windows is the correct instrument.

### GUI issues

#### G10-18 — GUI originally had no editor (hasEditor=false)

- **Symptom** — DSP present but `hasEditor()==false`, `createEditor()==nullptr`. [USER]
- **Root cause** — pre-commit Phase-1 state; not recoverable from Git (the first committed snapshot `d0be566` already implements the editor). A stale comment "none in Phase 1" survives at `G10Processor.h:127`, contradicting implementation at 477-478. [UNAVAILABLE]/[CODE]
- **Fix** — `d0be566` added `G10Editor`; `createEditor()` returns it; `hasEditor()` true (G10Processor.h:477-478, G10Processor.cpp:16-20).
- **Proof** — `G10EditorHookTests::testEditorHook` (41-64); HostSmoke real hosted editor (Main.cpp:405-411). [TS]
- **Lesson** — distinguish absent history from a committed defect; never trust architectural comments over executable code.

#### G10-19 — GUI lifecycle / headless test crash

- **Symptom** — crash constructing GUI objects headlessly. [USER]
- **Root cause** — G10 visual objects register with `ApexPresentationClock`, whose receiver methods assert the JUCE message thread (`ApexPresentationClock.h:164-167, 195-198`); constructing them without GUI initialization is invalid. [CODE, qualified inference]
- **Fix** — editor tests create `ScopedJuceInitialiser_GUI` (`G10EditorHookTests.cpp:28-37`); HostSmoke initializes GUI for the whole process (Main.cpp:661-664); receivers unregister in destructors; controls close unfinished host gestures.
- **Proof** — editor create/destroy/recreate tests; 32-cycle reverse-order teardown (`Main.cpp:602-657, 838-850`). [TS]/[TAG]
- **Lesson** — keep DSP-only tests separate from editor tests; initialize and tear down JUCE GUI on the message thread.

#### G10-20 — Faders initially invisible (editor-space coordinates in local components)

- **Symptom** — faders invisible at startup. [USER]
- **Root cause** — verified geometrically: a child fader at editor Y 278 with height 229 would clip a track drawn at editor Y 312-475 entirely below local Y 229. [D-derived]
- **Fix** — first committed version translates editor coordinates to child-local coordinates (`G10BandFaderComponent.cpp:8-17, 58-67, 95-163`; child bounds in `G10Editor.cpp:34-41, 291-305`); resulting local geometry (top 34, zero 116, bottom 197, gain text 213) fits height 229.
- **Proof** — static coordinate proof strong; no retained screenshot. [TS only]
- **Lesson** — every custom child must paint and hit-test exclusively in local coordinates; translate once at the component boundary.

#### G10-21 — Input/Output text overlap

- **Symptom** — knob label overlapped the body. [USER; geometry in d0be566]
- **Root cause** — old body center/radius (cy=30, r=22 → Y 8-52) overlapped the label (Y 2-14) by 6 px (`d0be566 G10RotaryKnobComponent.cpp:35-41`).
- **Fix** — `a838ebe`: body center 35, radius 18.5 (Y 16.5-53.5), label 0-11, value 59-73 (G10RotaryKnobComponent.cpp:63-83, 133-140).
- **Proof** — HostSmoke captures actual-size −18/+18, −6/+6, unity frames (Main.cpp:452-459); checks write success only; images not retained. [TS]
- **Lesson** — establish non-overlapping label/body/value rectangles numerically, then retain actual-size visual evidence.

#### G10-22 — Bipolar knob arc rendering wrong

- **Symptom** — arc wrong despite the unity constant being correct. [USER]
- **Root cause** — mixed angular conventions: old mapping used −225°..45° (unity −90°) while the JUCE path track used −135°..135°; pointer used conventional cos/sin, paths used JUCE's zero-at-12-o'clock convention (`d0be566 G10RotaryKnobComponent.cpp:16-21, 57-75, 89-95`).
- **Fix** — one convention controls track, value arc, unity tick, and pointer (`G10RotaryKnobComponent.cpp:8-14, 46-54, 86-130`).
- **Proof** — HostSmoke captures opposite cut/boost directions and unity (Main.cpp:452-459); no pixel/angle assertion retained. [TS]
- **Lesson** — normalize all rendering to one graphics-library angle convention before drawing any dependent geometry.

#### G10-23 — Double-click reset issue

- **Symptom** — double-click reset did not behave as an exact reset. [USER]
- **Root cause** — JUCE delivers the second `mouseDown` before `mouseDoubleClick`; old `mouseDown` always armed dragging and `mouseDrag` did not require `dragging_`, so a reset could overlap drag/gesture state (`d0be566 G10BandFaderComponent.cpp:181-190`, `G10RotaryKnobComponent.cpp:160-169`).
- **Fix** — reject multi-click `mouseDown`; require `dragging_` before drag edits; close any active gesture before resetting; faders reset to explicit exact unity 0.5; destructors end orphaned gestures (`G10BandFaderComponent.cpp:50-55, 166-204, 217-243`; `G10RotaryKnobComponent.cpp:39-44, 156-218`).
- **Proof** — real native double-click assertions for all ten faders from both +6 and −6 dB (`Main.cpp:461-496`); no equivalent native knob assertion retained. [TS]
- **Lesson** — double-click is an event sequence, not an isolated callback; reset logic must explicitly supersede pending drag/gesture state.

#### G10-24 — Mouse wheel support

- **Symptom** — custom controls had no wheel path. [USER; absent in d0be566]
- **Fix** — dominant wheel axis; reversed scrolling; Shift fine adjustment; clamp normalized values; one balanced host gesture per wheel event; ignore wheel while dragging (`G10BandFaderComponent.cpp:19-37, 245-263`; `G10RotaryKnobComponent.cpp:13-28, 221-237`).
- **Proof** — native Windows wheel tests: all ten faders, both trims, direction reversal, endpoint clamping (Main.cpp:498-576). [TS]
- **Lesson** — custom controls must implement accessibility-adjacent interaction paths explicitly and route every edit through the real hosted parameter.

#### G10-25 — Evil-eye handles reading as balls/gems/LEDs

- **Symptom** — handles looked like decorative balls/gems/LEDs, not eyes. [USER (subjective); old geometry committed]
- **Root cause** — circular aura and shell ellipses, tiny white iris/circular pupil, decorative diamond rune, strong colored aura (`d0be566 G10EyeHandleComponent.cpp:80-126`).
- **Fix** — `makePredatoryEye`: restrained almond aura/lid, pale sclera, colored iris, vertical slit pupil; diamond removed (`G10EyeHandleComponent.cpp:8-25, 89-157`); blink cadence changed from a 370 ms marching stagger on 7.4 s cadence to irregular first events at 4.8-32.1 s and 29-43 s repeats (`G10Editor.cpp:8-18, 163-174`).
- **Proof** — HostSmoke defines a real-editor blink frame sequence (Main.cpp:428-438); PNGs not retained. [TS]
- **Lesson** — perceived object identity comes from silhouette, material distribution, and motion cadence — not comments naming it an "eye".

#### G10-26 — Analyzer originally looked like a basic black rectangle/grid

- **Symptom** — analyzer presented as a minimal grid/trace over the near-black shell. [USER; verified in d0be566]
- **Root cause** — measurement existed but presentation was minimal (ten full-height divisions, baseline, fill, trace; `d0be566 G10SpectrumAnalyzerComponent.cpp:219-300`).
- **Fix** — cached near-black violet/maroon chamber, low-contrast radial depth fields, floor-fading log-frequency landmarks aligned to the ten EQ bands, rounded rim/vignette, cached fill/trace geometry, two restrained strokes; composite-only paint (`G10SpectrumAnalyzerComponent.cpp:288-375, 377-424, 426-460`).
- **Proof** — HostSmoke captures analyzer frames at blocks 8/16/32/64/90 (Main.cpp:579-588); numeric tests prove measurement, not appearance. [TS]
- **Lesson** — keep analyzer truth and analyzer aesthetics separate; cache static scenery and rebuild only bounded dynamic geometry.

### Analyzer latency ghost bug

#### G10-27 — Analyzer startup latency ghost bug

- **Symptom** — the analyzer showed stale/history-dominated visualization at startup; baseline T0→T4 = 1783.821 ms. [baseline [USER]]
- **Root cause** — a verified throughput deficit plus a stale-overflow policy:
  1. **Rate deficit [derived]**: old consumer drained 1024 frames per 30 Hz tick = 30,720 frames/s against a 48,000 frames/s producer → deficit 17,280 frames/s (old tick `d0be566 G10SpectrumAnalyzerComponent.cpp:87-112`, timer `G10Editor.cpp:95`).
  2. **Stale overflow [HC]**: old FIFO preserved oldest unread samples and dropped **newest** blocks when full (`d0be566 G10AnalyzerFifo.h:19-20, 52-53, 73-74`) — fresh signal rejected while stale history survived.
  3. **Hidden smoothing debt [HC]**: raw targets below the visible −60 dB floor allowed internal levels to sink toward ~−240 dB while the display clamped at zero (`d0be566 G10SpectrumAnalyzerComponent.cpp:192-200`).
  4. **Missing lifecycle reset [HC]**: old `createEditor()` enabled production at construction; hide/show did not clear FIFO/accumulation/display state.
- **Fix** — newest-window freshness policy: `drainNewestFrames` copies only the newest ≤2048 frames but consumes through the write boundary, discarding older history (`G10AnalyzerFifo.h:166-213`); visibility boundaries disable production, reset FIFO/local state, then re-enable when shown (`G10Editor.cpp:119-146`; reset at `G10SpectrumAnalyzerComponent.cpp:88-93`); presentation targets clamped to visible [−60, 0] (235-243); at most one analysis cycle per tick (two FFT calls, L and R, at 136-141 — "one FFT" is colloquial); `G10AnalyzerTimingProbe` correlates T0 (accepted push) → T1 (consume) → T2 (FFT) → T3 (cache) → T4 (render).
- **Proof** — newest-window test (three stale 100 Hz windows + newest 10 kHz → one tick empties FIFO and reports 10 kHz, `G10AnalyzerTests.cpp:498-524`); visibility reset (526-549); hidden-debt (551-589); correlated timing test (591-724). Final T0→T4 = 32.561 ms recorded in the tag annotation. [TS]/[TAG]
- **Qualifications** — the timing test enforces ordering/completion within an 8 s timeout, **not** a 32.561 ms ceiling; T0 is the first accepted push; T4 is direct offscreen `paint()` into a reused image; drop-newest remains the producer overflow policy after long stalls. [CODE]/[TS]
- **Lesson** — analyzer queues need an explicit freshness policy, correlated stage timing, a meaningful latency threshold, and retained raw results. A passing order-only timing test is not a latency regression gate.

### VST3 / HostSmoke issues

#### G10-29 — VST3 duplicate Bypass

- **Symptom** — external wrapper exposed an additional host bypass instead of using the existing `g10.bypass`. [USER]/[CODE comment at G10Processor.h:466-468]
- **Root cause** — no `getBypassParameter()` override; JUCE's VST3 client synthesized its own host bypass (verified absence of the override at `e4176c2` by `git grep`).
- **Fix** — `juce::AudioProcessorParameter* getBypassParameter() const override { return params_[kBypass]; }` (`G10Processor.h:466-469`).
- **Proof** — pointer-identity assertion (`G10ProcessorTests.cpp:205-206`); HostSmoke fixes bypass at hosted index 12 and verifies hosted bypass changes audio (Main.cpp:17-32, 787-790). [TS]/[CODE]/[GIT]
- **Lesson** — a processor's bypass DSP parameter and the format's designated bypass parameter are separate contracts; the wrapper must be told which existing stable parameter owns bypass semantics.

#### G10-30 — HostSmoke destruction heap corruption GHOST BUG

- **Symptom** — crash appeared during `HostToClientParamQueue` destruction / `RtlFreeHeap` at teardown. [USER; no CDB transcript retained]
- **Actual root cause** — found by ASan: HostSmoke declared maximum block 512 and later submitted 1024/4096 through the JUCE VST3 host wrapper. The heap was corrupted **earlier during processing** and only detected during destruction. [USER (ASan finding); recipe [ARTIFACT]: `.G10VST3HostSmoke.asan.jucer` with `/Zi /fsanitize=address`]
- **Fix** — separate `kProcessBlock = 512` from `kMaximumBlock = 4096`; `createPluginInstance(description, 48000, 4096)`; every instance prepared for 4096; hosted variable blocks {513, 1024, 4096} are legal because ≤ declared maximum (Main.cpp:17-20, 209-220, 853-859). The direct-processor oversized defense remains a separate layer (`G10EngineTests.cpp:260-337`).
- **Proof** — final source enforces the corrected maximum consistently; ASan build recipe existed; final HostSmoke run on the exact bundle exited 0 with `failures=0` and clean destruction (default, full 16, repeat 8×32, published-path smoke). [RUNTIME]/[CODE]/[ARTIFACT]
- **Lesson** — **where memory corruption is detected may not be where it started.** When heap damage is reported at destruction, locate the first invalid write. Defensive plugin chunking and the host wrapper's negotiated maximum are different layers; never intentionally violate a host API contract to test processor robustness.

#### G10-31 — Isolated HostSmoke matrix

- **Symptom** — late crash appeared correlated with multi-instance count or destruction (investigated at 5/6/7/8/9 instances). [USER]
- **Root cause** — apparent threshold contaminated by earlier heap damage in the normal sequence (G10-30).
- **Fix** — isolated dimension modes in one process: `editors` (editor lifecycle only), `normal` (audio + 1 parameter write), `heavy` (audio + 128 writes), `full` (audio + parameters + editors), `repeat` (audio-only lifecycle, 32 cycles); explicit mode/count dispatch; reverse editor destruction; `releaseResources()` + reverse instance reset (Main.cpp:602-657, 699-713); default matrix {1, 4, 8, 16} (861-881); repeated lifecycle 8 instances × 32 cycles (609, 712).
- **Proof** — final tag records "Instance matrix: 1/4/8/16 PASS" and "Repeated lifecycle: PASS (8 instances x 32 cycles)"; validation-session runs observed `failures=0` for full 16 and repeat 8×32. [TAG]/[RUNTIME]/[CODE]
- **Lesson** — for a ghost threshold bug, isolate dimensions and bypass earlier test phases. Count correlation is not causation when an earlier operation may already have corrupted process state. Isolation avoids falsely blaming G10 itself.

#### G10-32 — External Analog/Quality parameter exposure

- **Symptom** — the external product needed 13 canonical public controls while Native required the 15-parameter compatibility contract; deleting the two legacy objects would break state compatibility. [USER]/[CODE]
- **Root cause** — original processor coupled parameter ownership, host enumeration, and serialized state into one list; stock JUCE 8.0.12 provides no hidden-parameter surface (`G10Processor.h:181-186`).
- **Fix** — `ParameterExposure::{nativeCompatible, publicVst3}` (`G10Processor.h:142-160`); external factory picks `publicVst3` (`G10VST3Entry.cpp:8-12`); Native default unchanged; Analog/Quality hosted only for Native (192-215), state-owned otherwise (`stateOnlyParameters_`, 175-187, 605-608); hosted→semantic index mapping (539-547); state serialization still iterates all 15 fields (482-525); DSP ignores legacy values (267-275, 304-323); editor maps compact hosted indexes (`G10Editor.cpp:181-190`).
- **Proof** — Native 15-ID order (`G10ProcessorTests.cpp:85-160`); external 13 exact IDs + mapping + no public legacy + Native↔external state survival (164-228); real hosted VST3: 13 parameters with stable converted VST3 IDs (Main.cpp:723-753); legacy values writable in component state, survive resave, bit-inert in hosted DSP (806-836). [TS]/[CODE]/[GIT]/[RUNTIME]
- **Lesson** — **hiding legacy state is not the same as deleting it.** The public automation surface and opaque compatibility state are distinct schemas; legacy state must remain owned and round-trippable without being externally writable or musically active.

#### G10-33 — External automation→GUI index mismatch (additional)

- **Symptom** — host automation of Bypass could leave the bypass UI stale in the external format. [HC/SC]
- **Root cause** — old editor used the hosted parameter index directly; external removal of Analog/Quality moved Bypass from hosted 14 to hosted 12 (`d0be566 G10Editor.cpp:163-170`).
- **Fix** — map through `getSemanticParameterIndex` (G10Processor.h:539-547; G10Editor.cpp:181-190).
- **Proof** — mapping contract test (`G10ProcessorTests.cpp:164-228`). [TS]/[CODE]

### Additional significant DSP findings (G10-34 to G10-37)

- **G10-34 — Bypass crossfade state discipline**: bypass crossfade lives in the chain at base rate; reactivation must converge back to the never-bypassed reference (<1e-3, full response not ratio); dry output bit-exact after transition; "the bypass crossfade freezes the chain state" (`G10EngineTests.cpp:970-1001`).
- **G10-35 — Reused-processor 16 kHz droop**: q=1 reused processor measured −34.5 dB at 16 kHz at crossfade m≈0.785, root-caused as ~180° clean-vs-chain phase cancellation (`G10AnalogTests.cpp:1722-1797`); localized by selective component state reset (1643-1720); mitigated because `qualityMix_` snaps to an endpoint at prepareToPlay — no mid-stream crossfade in the real host flow.
- **G10-36 — Test feedback loop**: a previous numerical-safety test fed output back as input, causing geometric growth to Inf; corrected to clear the buffer before every `processBlock` (`G10EngineTests.cpp:1151-1165`).
- **G10-37 — responseFactor bias metric**: the boost-vs-cut bell metric is intrinsically biased (rBoost > rCut by ~0.13-0.16 at equal Q, "verified by svf_bias_check.ps1" — script [UNAVAILABLE]); replaced by direct Q introspection for boost/cut focus contracts (`G10EngineTests.cpp:67-88, 596-654`); the old "r(+12) < r(+6) − 0.03" threshold was itself the defect (610-629). Related: FOCUS proportional-Q monotonicity verified by Q introspection Q(+1)=0.70 → Q(+12)=1.25.

## 7. Validation, freeze, and publication process

### Evidence matrix (per Brain §28/§41 layers)

| Layer | G10 evidence | Proves |
|-------|--------------|--------|
| Direct processor/unit | `Tests\Source\G10` (nine files) | Processor-owned contracts, deterministic DSP, latency/timing stage order [TS] |
| APEX Native integration | `G10FormatHostTests.cpp` | Native format/chain integration (note: pre-existing chain-restore defect separate) |
| Packaged hosted-binary | HostSmoke `Main.cpp` | Real `.vst3` discovery, instantiation, 13 parameters + stable IDs, state round-trip, editor, blocks, 1/4/8/16, lifecycle [TS]/[RUNTIME] |
| Format validators | None tracked | — [UNAVAILABLE] |
| Representative real hosts | User manual confirmation in a real third-party DAW | Opens as VST3 [USER] |
| Exact published artifact | SHA-256 `E7B04B...48F4` verified at destination at publication | Byte-identical published module [RUNTIME]/[ARTIFACT] |

### Release gates passed (per tag annotation + validation session)

- HostSmoke PASS (13 parameters, no Analog/Quality public, legacy state round-trip, bit-inertness, mono/stereo, all ten bands, Input/Output/Bypass, editor open/close/reopen, blocks 513/1024/4096, 1/4/8/16 instances, clean destruction, exit 0) [TAG]/[RUNTIME]
- Repeated lifecycle: 8 instances × 32 cycles PASS [TAG]/[RUNTIME]
- Analyzer final latency: T0→T4 = 32.561 ms [TAG]
- Third-party DAW: MANUAL USER VALIDATION PASS [USER]

### Publication mechanics

`Scripts/build_g10_vst3.ps1` publishes transactionally: stage → move existing to backup → move staging into destination → delete backup only after replacement → restore backup on failure (lines 112-143); hashes the build module (145-147). The 2026-08-09 publication additionally verified the staged module hash and the published module hash byte-for-byte against the accepted value before the retained rollback was removed.

**Caveats recorded** — staging validation in the script checks directory existence only; no fault-injection publication test; publication is a two-rename sequence with a temporary no-destination interval; the absolute destination path is environment-specific.

### Known evidence gaps (do not silently fill)

- No raw HostSmoke stdout, ASan report, CDB transcript/dump, or matrix logs are archived in the repository. [UNAVAILABLE]
- No tracked release manifest/SBOM/provenance record exists yet. [UNAVAILABLE]
- No named third-party DAW/version or screenshot in the repository. [USER]
- `evidence\runs` is intentionally ignored by `.gitignore` (lines 49-54). [CODE]

## 8. What worked

- Single-ownership processor: one DSP, state, parameter, editor implementation reused by Native and VST3 (`G10VST3Entry.cpp` returns `G10Processor` directly; no delegating processor).
- Committed test contracts lock the fixed behavior even though bug histories were not committed.
- Ghost-bug discipline (first-cause diagnostics, ASan) saved the plugin from a false processor indictment.
- Newest-window analyzer freshness with visibility-ownership reset turned a 1783.821 ms ghost into a 32.561 ms real path.
- Parallel-dry crossfade design proved by endpoint bit-exactness.
- Transactional publication with verified hashes and rollback retention.

## 9. What failed or created avoidable risk

- HostSmoke initially violated its own declared host-buffer contract (G10-30) — the most expensive lesson.
- Intermediate HostSmoke revisions were never committed, so the forensic chronology is partly unrecoverable.
- No retained raw evidence packages (logs, ASan output, CDB, screenshots, manifests) for future audits.
- Document drift: `G10Processor.h:127` still says "none in Phase 1" (editor exists); `G10SpectrumAnalyzerComponent.h:53-56` describes an older rendering (two strokes, direct measured-point segments at `G10SpectrumAnalyzerComponent.cpp:408-412, 446-452`).

## 10. Rejected hypotheses and unresolved unknowns

- **Rejected** — G10 processor itself caused the HostSmoke destruction crash (actually a wrapper contract violation).
- **Rejected** — instance count 5/6/7/8/9 was a threshold bug (actually contaminated by earlier corruption).
- **Rejected** — H5/H9/H15 bins were aliases (they are saturation harmonics).
- **Rejected** — impulse/static gains predict the nonlinear response (level-dependent compression).
- **Rejected** — low-frequency impulse FFT curves were real (window/leakage artifacts).
- **Open [SC]** — `G10Parameter::value_` is a plain `float` written on the control thread and read on the audio thread; no explicit synchronization or concurrency test is present in the committed tree. Not part of any milestone assertion; treat as an open static finding.
- **Unknown** — exact pre-fix failure magnitudes for issues 1-2, 5-6, 10-14, 16, 18-19, 25, 28-31 are not recoverable from Git (G10 landed atomically in `d0be566`).

## 11. Learned invariants (must survive G10)

1. Never retune DSP because one suspicious test failed — validate the test first.
2. Ghost crashes require first-cause diagnostics (ASan/CDB); the detection site is not the origin.
3. Preserve stable state IDs across persistence and exposure boundaries; hiding legacy state ≠ deleting it.
4. Real host validation and real-editor visual review matter; a controlled harness is not a DAW matrix.
5. Analyzer/meter freshness is a throughput contract (producer frames/s vs consumer frames/s).
6. Performance and multi-instance behavior are product features, not afterthoughts.
7. Freeze/hash/tag known-good milestones; a tag annotation is a claim, raw evidence is the archive.
8. Do not let agents erase historical evidence after a fix; commit the absurd numbers as warnings.
9. Direct processor robustness tests and VST3 host-contract tests are different layers; never violate a host API contract to test processor robustness.
10. Realtime paths allocate nothing: preallocated scratch, lock-free FIFO, chunked oversized blocks, denormal flush.

## 12. Final G10 record

| Field | Value |
|-------|-------|
| Canonical Native freeze | `g10-canonical-freeze` → `e4176c2569a5cd1f3cb5cf43e6367f5e0312a1a1` |
| Windows VST3 milestone | `g10-windows-vst3-1.0` → `a838ebe43af3f24526135fd4da3180f564c80f25` |
| Published path | `C:\Users\1993v\OneDrive\Desktop\Apex backup\Apex plug-ins\Windows\APEX G10.vst3` (environment-specific) |
| Published SHA-256 | `E7B04B5674A2F644217CB40805645C2C15304DBE97FF162894E6C497AEEF48F4` |
| Module | x86-64, version 1.0.0, 5,731,840 bytes, Authenticode NotSigned |
| moduleinfo.json SHA-256 | `39B01FF609EE30E2BC25EF49DC871969960EFBC005D42D53BF2293562D12BA24` |
| Public VST3 parameters | 13 (Input, 10 bands, Output, Bypass) |
| Native compatibility | 15 parameters preserved |
| Analyzer latency | T0→T4 = 32.561 ms |
| HostSmoke | PASS (validation session: failures=0, exit 0) |
| 1/4/8/16 instances | PASS |
| Repeated lifecycle | 8 × 32 cycles PASS |
| Third-party DAW | MANUAL USER VALIDATION PASS |

## 13. G10 final architecture and debugging sequence (2026-08-11)

This chapter records the completed G10 architecture as it existed at the end
of the Mini Clean EQ / UX / metering cycle, and the debugging sequence that
transformed G10 from a 10-band EQ into the finished product. It is an
engineering record: every claim below is backed by the source tree, the test
suites, the retained artifacts, or explicitly marked [USER].

### 15.1 Final architecture summary

- **Frozen musical 10-band core** — DEEP (31) through AIR (16k), ten
  permanent musical families with committed per-family curve laws
  (`G10CurveEngineCore.h`; `G10EngineTests` family contracts). The musical
  core was never retuned during the Mini EQ / UX cycle; it remained the
  product identity.
- **D1B/I1 color architecture** — the always-on analog character chain:
  D1B discrete input stage (tanh residual + even asymmetry, 2 Hz DC blocker
  on the residual only) and I1 iron output stage (flux-modulated saturator,
  strength capped at 0.95, monotonic to x=8). Realtime 2x / offline 4x
  oversampling with quality snapped from `isNonRealtime()` at prepare so the
  start state is click-free. Legacy `g10.analog`/`g10.quality` are
  serialized but DSP-inert.
- **Native/VST3 parameter contract** — one processor, one parameter
  implementation, two exposure policies: Native 15 parameters (compatibility
  ABI) and external VST3 30 public parameters (the v3 Mini EQ expansion;
  Analog/Quality remain state-only). Stable IDs cross persistence and
  exposure; `getBypassParameter()` owns the bypass authority.
- **Mini Clean EQ** — a bounded clean digital correction layer positioned
  AFTER the oversampled musical/color path and BEFORE the analyzer tap:
  HPF, up to 3 Bell nodes, LPF, with ~5 ms realtime-safe ramps and a settled-
  OFF bit-identity skip. State schema v3 with semantic v2→v3 migration.
- **State schema v3** — HPF/LPF full-spectrum law `20·1000^p` Hz
  (20 Hz..20 kHz, p==0 / p==1 OFF sentinels), Bell Q `0.10·400^p`
  (0.10..40.0, default 1.0), Bell frequency `20·1000^p`, gain −12..+12 dB.
  v1 states keep Mini EQ defaults; v2 states migrate old normalized → exact
  old Hz/Q → new normalized.
- **Bell bypass semantics** — bypass is not delete: the Bell stays present,
  Frequency/Gain/Q persist, the DSP contribution fades to identity
  (snap-on-exit while the mix is zero), and re-enable resumes from the
  edited configuration. Delete removes the Bell and frees the slot.
- **Analyzer** — post-output power-sum measurement, 140 log bars, newest-
  window freshness, presentation-clock rendering, band-aligned landmarks.
- **Rendered curve/node synchronization** — one pixel authority chain:
  `responseDbToPlotY(db)` → `getNominalCurvePointAtHz(hz)` →
  `getBellNodePosition()`; the curve polyline inserts every enabled Bell
  centre frequency so Q40 peaks cannot fall between samples. Invariant
  VISIBLE CURVE PIXEL == NODE CENTER == HIT-TEST CENTER == PANEL ANCHOR.
- **Inspector / full / HUD UX** — selected idle: compact full inspector with
  smart non-obstructive placement (allowed families ABOVE / TOP-LEFT /
  TOP-RIGHT / LEFT / RIGHT; never below the Bell; stable against cursor
  movement; frozen during panel-origin gestures); graph drag: micro HUD;
  hover-only: node highlight, no panel; click empty graph: deselect.
- **Double-click Bell creation** — the Bell is born exactly where the user
  clicks: click X → Frequency, click Y → Gain (same plot geometry), default
  Q 1.0, bypass false, enabled true.
- **Input/output metering** — INPUT meter = real level entering internal
  processing after Input Trim; OUTPUT meter = the exact final level delivered
  to the host. Audio thread measures per-block channel peaks (louder of L/R)
  and atomically publishes (relaxed floats — no locks, no allocation); UI
  performs fast-attack / ~280 ms release / ~600 ms peak-hold ballistics with
  quantized repaint.
- **Resize/host lessons** — logical content bounds + a single Content
  transform; host window size, logical size, and host scale factor kept
  separate; preferred bus-config ordering validated against real host
  metadata.

### 15.2 The debugging sequence that finished G10

1. **Curve/node pixel mismatch (real-user screenshot)** — the Bell nodes
   visibly floated above the response peaks. Root causes found in the paint
   path: (a) the curve used full-surface-width X coordinates while nodes used
   plot-relative X; (b) uniform 257-point sampling could miss narrow high-Q
   peaks entirely. Fix: one plot-space authority for curve, nodes, hit-test
   and panel anchor, plus Bell-frequency insertion into the curve sample set.
   Regression: render-level test reads the actual painted curve vertices and
   asserts the node centre coincides within 1 px.
2. **Sample-rate lifecycle bug** — `getSampleRate()` returned 0 (JUCE's
   `setRateAndBufferSizeDetails` was never called), making the surface
   evaluate all cascade math at 1 Hz. Fix: call
   `setRateAndBufferSizeDetails(sampleRate, samplesPerBlock)` in
   `prepareToPlay`; the UI uses the processor's real prepared rate.
3. **Memory corruption (ASan case)** — an intermittent 0xC0000005 in the
   `LookAndFeel` destructor turned out to be delayed heap corruption. ASan
   found the FIRST invalid write: `BellControlPanel::~BellControlPanel` →
   `commitEditor` → model callback → `rebuildCurve` → writing into
   `curvePath_` after its destruction (member destruction order).
   Fix: quiet editor teardown that never commits or notifies owners during
   destruction. ASan was also used to prove HostSmoke's declared-max
   violation (G10-30).
4. **Inspector self-movement bug** — adjusting FREQ/GAIN/Q from inside the
   panel moved the node, which triggered smart placement, which moved the
   panel under the user's active control. Fix: a panel-origin interaction
   lock freezes the panel bounds for the whole gesture; placement recomputes
   once after the gesture ends.
5. **Placement stability and cursor runaway** — the panel avoided the cursor
   so aggressively it became hard to use, and could be placed below the
   selected Bell. Fix: remove the cursor penalty, restrict full-inspector
   families to ABOVE / TOP-LEFT / TOP-RIGHT / LEFT / RIGHT (never below),
   and add a stability key so cursor movement alone never reflows the panel.
6. **Double-click creation position** — the new Bell ignored the click Y.
   Fix: click Y → Gain via the canonical plot inverse
   (`yToGainNorm`), X → Frequency via `xToFreqNorm`; verified by real
   double-click path tests across the graph.
7. **Band-fader double-click reset** — verified that double-clicking any of
   the ten band faders resets that band to exactly 0 dB through the
   canonical gesture pipeline (real mouse path, all ten bands, +6 dB armed).
8. **Metering tail lesson** — a test fed in-place processed output back as
   the next "silence" block and misread the legitimate filter tail as a
   meter bug. Fix: clear the input buffer before every `processBlock` when
   feeding repeated silence blocks.

### 15.3 Final validation philosophy

- Automated tests verify objective contracts (geometry ≤1 px, parameter
  values, state migration, meter levels, placement families, lifecycle);
  human eyes approve visual balance, obstruction, and perceived alignment —
  a separate, explicit gate.
- Evidence gates used at the end of the cycle: G10.Processor and
  G10.EditorHook suites (seed 0xA9E12026), MSVC AddressSanitizer runs,
  HostSmoke (331 PASS / 0 FAIL on the packaged hosted VST3), and a fresh
  APEX Release rebuild for user review. The Mini EQ / UX / metering cycle
  never modified the frozen musical DSP.

## 14. Corrections history

| Date | Change |
|------|--------|
| 2026-08-09 | Initial permanent postmortem written from git/source/test evidence; user-supplied failure episodes explicitly marked [USER]; evidence gaps listed in §7/§10. |
| 2026-08-11 | Final architecture and debugging sequence chapter appended (§15) covering the Mini Clean EQ, curve/node synchronization, inspector/HUD UX, double-click creation, input/output metering, and the memory-corruption / sample-rate / panel-lock debugging sequence. |

## 15. References

- `docs/APEX_PLUGIN_ENGINEERING_PLAYBOOK.md` — the reusable process distilled from this case.
- `Plugins/G10VST3/README.md` — product identity, build, and publication.
- `Scripts/build_g10_vst3.ps1`, `Scripts/run_apex_tests.ps1` — build/test/publish contracts.
- DAW Brain v7.0.0-alpha.1: §4 (3992-5575), §21 (28008-29206), §27 (35773-37355), §28 (37356-38964), §34 (45912-47416), §37 (50249-51805), §41 (56094-57370), §42 (57371-59408).
