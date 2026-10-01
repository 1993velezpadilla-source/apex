# APEX C4 — Completion Report & Permanent Postmortem (Phase 9)

Status: **APEX C4 — COMPLETE** — declared 2026-08-13.

## Final plugin identity

- Name "APEX C4", format "APEX Native" (unified family format), uniqueId
  `0x4334` ("C4"), category EQ, 2-in/2-out, zero latency, 19-parameter frozen
  ABI, Console Character Equalizer (WEIGHT / SCULPT / BITE / OPEN + BLOOM).

## Final production sound (Phase 5, frozen)

BLOOM default 4.0 · HALO softness 0.5 / amount unity / ratio 0.90 ·
coupling 0.50/0.32/1.1/1.6 · color W/S/B/O 0.055/0.055/0.050/0.028 ·
evenW 0.75/0.65/0.50/0.40 · driveRefDb −12 · activationExp 0.85 ·
cutSuppression 1.0 · bloomLaw 0.85 · residual oversample 2x (zero latency).
Geometry: WEIGHT 0.50/0.80 @1.20 late-proportional; SCULPT 1.10/1.30 @0.90;
BITE 0.30/0.45 @1.00 near-constant-Q; OPEN 0.35/0.40 @1.00.

## Final GUI (Phase 7, user-approved)

Vertical console: four tall strips (bronze/amber/red-orange/ice-blue),
supporting top utility row, gold BLOOM bottom rail, live gain dB readouts,
Spectrum Flag expanding DOWNWARD (660x640 compact / 660x880 expanded,
min 560x640) without ever shrinking the console.

## Final Spectrum Flag (Phase 6, frozen)

Closed/Pre/Post/Both; realtime-safe mono-mix taps; SPSC rings; low-priority
analyzer worker (4096-pt Hann FFT, per-stream smoothing, double-buffered
snapshot); analytic linear response curve; dormant when closed; no FFT in
processBlock; observational only.

## Final host integration (Phase 8)

Unified `ApexNativePluginFormat` (one format object per name — the JUCE
format-manager name-collision fix); scanner seeds G10 + C4 deterministically
exactly once each; app Debug + Release rebuilt green.

## Final regression manifests (complete final gate set)

| Gate | Result |
|---|---|
| APEX.C4 Debug | GREEN — `d991f9b9fc86` |
| APEX.C4 Release | GREEN — `68fb09ba6dd6` |
| APEX.G10 Debug | GREEN — `d5d52a05ed5a` |
| Global Debug | GREEN — `95b309bb0fe4` |
| Global Release | GREEN — `3c6104054716` |
| App Debug build | PASS — exe SHA-256 `44BAACF90413...2425E92` |
| App Release build | PASS — exe SHA-256 `630D2B26D5D0...03B19DB4` |
| Repository policy | PASS |
| Dependency verification | PASS |

## Permanent postmortem — every lesson (for all future APEX native plugins)

1. **JUCE AudioBuffer clear-state bug**: `clear()` early-returns when the
   buffer is flagged cleared; engines that write via raw pointers never
   reset the flag, so the next flagged clear() is silently skipped and the
   previous output feeds back as input (the growing-IR investigation). Fix:
   explicit zero-fill in direct-engine tests (`clearBufferExplicit`).
2. **Smoother float stagnation**: a one-pole can stagnate below its target
   when the per-sample step falls below half an ULP. Fix: representable
   no-progress snap + tolerance snap in `C4Smoother` (exact convergence).
3. **Lifecycle buffer-channel bug**: buffer geometry assumptions across
   lifecycle tests — pin channels explicitly per test stage.
4. **HPF/LPF OFF normalization collision**: OFF=0.0 collided with the
   minimum cutoff normalization. Fix: `kHpfLpfNormFloor = 1/256` so real
   cutoffs map into (0,1] and OFF round-trips exactly.
5. **OPEN shelf measurement correction**: the parallel console topology's
   boost response never returns to 0 dB, so the classic −3 dB width is
   undefined for small boosts. Fix: half-excess width metric.
6. **Digital LPF near-Nyquist expectation correction**: a 24 kHz LPF at
   44.1 kHz is unrealizable; the engine clamps to 0.49·fs (~21.6 kHz) —
   documented TEMPORARY behavior, not a defect.
7. **Frequency parser issue**: text parse of band frequencies must use the
   band's log law and never the unit string.
8. **Active-range test mistake**: asserting untouched samples must run
   against the same block geometry the engine actually writes.
9. **Complex-response composition lesson**: magnitude-only superposition of
   neighboring band skirts is wrong whenever the skirts carry phase; the
   composition audit uses the COMPLEX transfer (real + imaginary).
10. **Morph audit lesson**: bell<->shelf morph is a continuous blend of the
    SAME SVF states — never a topology switch; intermediate blends must be
    held at fixed points for measurement.
11. **Sine-phase-reset measurement artifact**: restarting sine phase across
    blocks smears FFT measurements; keep a single continuous phase counter.
12. **Bounded-envelope test correction**: transient tests must measure the
    crest inside the transient window, not the padded tail.
13. **Phase 5 coupling evidence adjustment**: measured valley fills
    (0.01-0.06 dB) were BELOW the perceptual floor (parallel skirts compress
    the contour ~2.5x); the amount was raised with a measured rationale
    (strength 0.50, maxContourDb 1.6) — never by intuition.
14. **Phase 6 shared PRE/POST smoothing-state bug**: one shared one-pole
    array coupled the PRE and POST histories, compressing the measured
    post-pre difference by exactly 0.29x (1.74 dB measured vs +6 expected).
    Fix: per-stream smoothing state. Lesson: never share filter state
    between streams that must stay independent.
15. **FFT transfer/scalloping lesson**: a spectrum-transfer estimate from a
    single FFT bin can produce false gain readings (independently windowed
    PRE/POST frames place Hann energy differently). Separate signal spectra
    from the ANALYTIC filter response; use a justified local-region
    estimator for transfer measurements.
16. **GUI horizontal-card rejection -> vertical-console redesign**: user
    review rejected four horizontal cards; the console-strip architecture
    (tall vertical modules, top-to-bottom reading) is the C4 visual
    identity. Include a real visual-review checkpoint with fresh captures
    and a live native preview BEFORE freezing any GUI phase.
17. **Gain-readout and analyzer-expansion corrections**: faders must show
    exact dB values live; opening an analyzer panel must EXTEND the window,
    never compress or hide core controls (fixed 430 px strips).
18. **Host integration — format-manager name collision**: JUCE's
    AudioPluginFormatManager keys formats by NAME and silently drops a
    second same-named format. Multiple plugins sharing a family name need
    ONE unified format object routing by description. Test every plugin
    THROUGH the manager, not just through its own format class.
19. **Test-infrastructure traps** (general): never capture a wait index
    AFTER feeding (no new data can arrive); respect inactive double-buffer
    halves; OneDrive cloud placeholders can serve stale bytes — write
    user-facing previews to a non-synced directory; force recompilation
    when timestamps lie; keep per-stream state, per-instance state, and
    real-time cadence in analyzer tests.

## Known limitations (honest)

- The real-musical listening corpus remains the single vocal source; the
  Phase 5 render pack is the reproducible support material and the final
  subjective listening pass remains a human activity.
- The in-APEX interactive host checklist (insert/open/save/reopen on the
  live DAW) is available as a manual run on the launched Release build; the
  automated host lifecycle is fully covered by the suites above.

## Document paths

- `docs/C4_PARAMETER_CONTRACT.md` (frozen ABI)
- `docs/C4_PHASE1_EVIDENCE.md`, `C4_PHASE2_HALO.md`, `C4_PHASE3_COUPLING.md`,
  `C4_PHASE4_BLOOM.md`
- `docs/C4_PHASE5_PERCEPTUAL_TUNING.md` (frozen sound + render pack)
- `docs/C4_PHASE6_SPECTRUM_FLAG.md`
- `docs/C4_PHASE7_GUI.md`
- `docs/C4_PHASE8_HOST_INTEGRATION.md`
- `docs/C4_PHASE9_COMPLETION_POSTMORTEM.md` (this file)
- Evidence: `evidence/runs/**` manifests, `evidence/renderpacks/C4_PHASE5/`
  and `C4_PHASE7_PREVIEW/`.
