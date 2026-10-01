# APEX Parametric EQ Phase 3 — Stereo / L / R / M / S Placement + Band Solo/Audition (PASS)

Status: **PHASE 3 = PASS** — declared 2026-08-14 as a diagnostic working-tree
milestone. The shared workspace remains intentionally dirty and has no clean
release commit/tag, so this is not release-grade evidence. Phases 1 and 2
remain frozen regression baselines.

## Scope

Phase 3 implements the original engineering mission:

- Per-band channel placement **Stereo / Left / Right / Mid / Side** for every
  compatible band across all 24 slots.
- **Band Solo/Audition** with the correct probe region for gain shapes and
  the exact removed-region signal for Low Cut / High Cut / Notch, working for
  every placement without M/S channel collapse.
- The **editor/controller** exposing placement and audition as contextual,
  touch-capable controls, architected from the start for Windows, macOS,
  Android phones/tablets, iPhone, and iPad.
- The **2×2 complex stereo transfer matrix** as the response authority.
- Phase 2 **analyzer prerequisite corrections** (stereo energy truth,
  publication ordering, mode epochs, freshness).

## Stereo / L/R / M/S architecture

- `Engine::process` keeps fixed slot order (0..23); mixed placements compose
  exactly as ordered matrix products. Left/Right operators do not commute
  with Mid/Side operators, so no regrouping occurs.
- Transfers (verified against measured impulse paths):
  - Stereo: `[[H, 0], [0, H]]`
  - Left: `[[H, 0], [0, 1]]` — the physical right channel is bit-exact wire.
  - Right: `[[1, 0], [0, H]]` — the physical left channel is bit-exact wire.
  - Mid: `0.5 * [[H+1, H-1], [H-1, H+1]]`
  - Side: `0.5 * [[H+1, 1-H], [1-H, H+1]]`
- Peak-safe matching M/S convention: `M = 0.5*(L+R)`, `S = 0.5*(L-R)`,
  `L = M+S`, `R = M-S`. Dual-mono encodes to Mid=x, anti-correlated encodes
  to Side=x, and identity bands skip the encode entirely so neutral paths
  remain bit-exact wire.
- Mono semantics: Stereo/Left/Mid filter the mono stream; Right/Side are
  exact wire (absent components are not aliased to mono).
- A band whose effective state domain changes (placement change) deliberately
  resets its filter history; domain-stable coefficient edits do not.
- Validated: mono, dual-mono, correlated, anti-correlated, left-only,
  right-only, center-only, side-only, mixed chains, alternating 24-band
  stress, six sample rates, and hostile block sizes.

## 194-parameter ABI and state v2

- Indices 0–169 are the frozen Phase 2 identities (unchanged).
- `peq.band01.placement` .. `peq.band24.placement` appended at indices
  170–193; total **194** hosted parameters, all lock-free atomic.
- Placement parameters are discrete five-step choices (Stereo..Side),
  non-boolean, defaulting to Stereo.
- State version 2: full round-trip of every placement; version-1 state loads
  with Stereo placement defaults (missing properties tolerated and
  non-destructive); malformed values clamp (out-of-range → Side,
  negative/NaN normalized → Stereo); foreign schemas cannot mutate state.
- Placement adoption uses the existing dual-engine 10 ms crossfade. A
  placement change arriving while a transition is audible is deferred
  (`pendingPlacementBands_`) and applied with a deliberate state reset at the
  transition boundary — no hard topology jump. Placement automation under
  settled global bypass adopts directly.

## Band Solo/Audition

- Transient, token-owned, **never a hosted parameter and never serialized**.
- Editor/controller publishes Begin/End through a packed lock-free mailbox;
  only the audio thread adopts at a block boundary. A matching-token End
  fades out; stale End tokens cannot cancel a newer gesture; a new Begin
  supersedes; disabled/bypassed bands are rejected.
- One preallocated single-band audition runtime (no third full engine):
  gain shapes audition the unity-probe residual `H_probe - 1`
  (probe = +6.0206 dB, tilt included); Low Cut / High Cut / Notch audition
  the exact removed signal `1 - H`; Band Pass auditions `H`; All Pass
  auditions `0.5*(1 - H)`.
- Placement-correct channel semantics: Mid audition decodes to L=R, Side to
  L=-R (no physical-channel collapse); Left/Right audition silence the
  unselected physical output.
- Exact 10 ms engage/release ramp (verified step-by-step at one-sample
  blocks); release converges bitwise to the never-auditioned reference;
  engage/hold/release allocate zero memory.
- Cancelled by: band disable (delete), band bypass, placement change, shape
  change, global bypass, processor reset/reprepare/release, state restore,
  explicit cancel, and editor destruction. Fresh processors are inactive.

## 2×2 response authority

- `ResponseCore::wetTransfer` composes per-band `StereoTransfer` matrices in
  fixed slot order; `responseMatrix` layers the transition and bypass
  crossfades; `midProjection()`/`sideProjection()` provide labelled scalar
  views. Measured 2×2 impulse paths match the analytic matrix for mixed
  Stereo/Mid/Side/Left/Right chains in both design modes (≤ 0.05 dB).
- `ResponseFrame` now also publishes channel count and audition state for the
  editor.

## Analyzer prerequisite corrections (Phase 2 defects)

- Stereo L/R frames replace the phase-cancelling `(L+R)/2` tap; separate
  L/R FFTs combine equal-power energy — anti-phase equals in-phase and
  single-channel is exactly 3.0103 dB lower (verified at six rates).
- Publication ordering: `waitForSnapshot` polls pinned `acquireSnapshot`, so
  a wait-visible sequence can never precede its slot; close/mode changes
  cannot report stale completions.
- Every tap-mode change is a new epoch; discontinuities and FIFO loss discard
  contaminated backlogs and mark the first fresh frame discontinuous.
- Newest-window freshness; pinned-slot publication drops no longer stall
  analysis age; `snapshotDropCount()` exposed.

## Responsive, touch-capable, mobile-ready editor

- Breakpoints: CompactPhone, LargePhone, Tablet, Desktop, LargeDesktop.
- Portrait-compact layouts use a bottom contextual sheet (the graph stays
  dominant); wide layouts use a side inspector; the inspector content is
  scrollable (juce::Viewport) instead of shrinking controls.
- Minimum 44 px logical touch targets; node hit areas are 44×44 while visuals
  stay small; node drag updates frequency/gain with balanced host gestures;
  long-press on a node auditions it; long-press on empty space creates a band.
- Placement is a five-way segmented control; audition is a momentary hold
  button; all essential interactions are single-pointer reachable (no hover,
  right-click, wheel, or modifier dependencies).
- Bounded consumption: the graph timer consumes published immutable
  `ResponseFrame`s (25 Hz) and pinned analyzer snapshots; the curve sampler
  inserts critical frequencies around every band (Q up to 100) and draws
  labelled Mid/Side projections when placements are mixed. No DSP runs on the
  render thread; the editor never touches mutable engine state.
- Editor destruction ends and cancels audition. State restore leaves the
  surface consistent. Layout tested at 320×568, 480×800, 768×1024,
  1280×720, 1920×1080: controls in bounds, no overlaps, no negative
  rectangles, graph dominant, placement/audition accessible.
- Physical Android / iPhone / iPad runtime validation remains a later
  platform-port requirement; the architecture is portable and the
  platform-independent behavior is tested now.

## Root-cause bugs found and fixed this phase

1. **Configuration erasure by history reset** — `AuditionBand::reset()`
   cleared settings/coefficients after `configure()` assigned them, so the
   adopted audition filter was immediately wiped and the next block cancelled
   the hold. Fix: `clearHistory()` (state only) split from `reset()` (full);
   `configure(resetState=true)` clears history without touching the design.
   Regression: `ParametricEQ.Audition / history reset never erases freshly
   configured coefficients`.
2. **Fast-path feature bypass** — the no-transition/no-bypass fast path
   called `currentEngine_.process()` directly, so `processAudition()` never
   ran and the mix could not ramp. Fix: the fast path also requires audition
   inactivity. Regression: the full audition suite + the exact-ramp-law test.
3. **Editor click wiring** — JUCE `triggerClick()` posts an async command
   message (silently dropped without a dispatch loop) and radio-grouped
   toggle buttons swallow `onClick`. Fix: placement/top-bar buttons use
   `setClickingTogglesState(false)` with manual toggle display in `refresh()`;
   tests drive the real click command path with a short dispatch pump.
4. **Audition-row vertical exhaustion** — `juce::Rectangle::removeFromTop`
   clamps to remaining height, leaving the audition row 32 px instead of
   48 px. Fix: inspector content height includes the margins plus every fixed
   row so the 44 px minimum target is always preserved.
5. **Uninitialized test-buffer excitation** — several test helpers fed
   uninitialized `juce::AudioBuffer` memory into `processBlock` while
   audition settling, so stack garbage (varying per run and call site)
   transiently excited filter states and produced run-dependent residuals —
   the moving full-suite Release failures. Instrumentation proved mix=1.0 and
   hold=true during the failing measurements, and a sample-exact cross-check
   localized the divergence. Fix: all affected test helpers now explicitly
   clear their buffers; the sample-exact gain-independence cross-check now
   proves bitwise-identical region outputs between 0 dB and +18 dB bands.
   This was a test-harness defect; no DSP change was required or made.

## Validation

All commands ran from `My DAW/DAW_Core` with seed `0xA9E12026`.

| Gate | Result |
|------|--------|
| Focused Debug `APEX.ParametricEQ` | PASS — 101 groups, 156,284 assertions, 0 failed |
| Focused Release `APEX.ParametricEQ` | PASS — 101 groups, 156,284 assertions, 0 failed |
| Complete repository Debug | PASS — 695 groups, 215,275 assertions, 0 failed |
| Complete repository Release (run 1 of 3) | PASS — 695 groups, 215,275 assertions, 0 failed |
| Complete repository Release (run 2 of 3) | PASS — 695 groups, 215,275 assertions, 0 failed |
| Complete repository Release (run 3 of 3) | PASS — 695 groups, 215,275 assertions, 0 failed |
| Debug application rebuild | PASS |
| Release application rebuild | PASS |
| `test_repository_policy.ps1` | PASS |
| `verify_dependencies.ps1` | PASS |
| `test_validate_test_evidence.ps1` | PASS — 6 passed, 0 failed |
| Phase 3 source hygiene (32 files) | PASS |
| Phase 3 tracked diff check | PASS |

The complete Release suite was executed three consecutive times with the
final sources; all three passed. The earlier moving failures were traced to
the uninitialized-buffer harness defect described above, not to DSP.

## Evidence run identifiers

| Scope | Configuration | Run ID | Exit |
|-------|---------------|--------|------|
| Focused `APEX.ParametricEQ` | Debug | (see focused-debug manifest) | 0 |
| Focused `APEX.ParametricEQ` | Release | (see focused-release manifest) | 0 |
| Complete repository | Debug | `2e28c70895e2` | 0 |
| Complete repository Release 1 | Release | `702f3c383c9f` | 0 |
| Complete repository Release 2 | Release | `03fdbcd26117` | 0 |
| Complete repository Release 3 | Release | `9ea34e8286b3` | 0 |

## Artifact inventory (UTC, SHA-256)

| Artifact | Bytes | Timestamp | SHA-256 |
|----------|------:|-----------|---------|
| Debug `DAW_Core.exe` | 39,782,912 | 2026-08-14 21:10:38 | `CBFD68F22D7F5A4C21AEA5CC1FFC0F60A9AB0BF2EC3DE1775680FE0D925B5F81` |
| Debug `DAW_Core.pdb` | 256,102,400 | 2026-08-14 21:10:38 | `ECD132AA4D161F1FE4F5CCB5BDDF4075954ABF1D43AE16FC5AB5B84F1681AF9C` |
| Release `DAW_Core.exe` | 13,334,016 | 2026-08-14 21:19:26 | `1C15BDD0E604BDF0BE22F79B90E49B87BC6A0F9735C88A0D4A8D7CD72B49E01E` |
| Release `DAW_Core.pdb` | 162,222,080 | 2026-08-14 21:19:26 | `C85DD5A5BD65F5A38635A9ABACF908E535E52A6D57318087CD0A3F52CF8D13A3` |
| Debug `APEXTests.exe` | 27,412,992 | 2026-08-14 20:36:39 | `1111B30BD37181ABC5A241BBE9F944F79F2C5BE0AC44A82B89C6F550F8578C1E` |
| Release `APEXTests.exe` | 9,576,448 | 2026-08-14 20:18:19 | `93A815B25480775D3D860CAE1A93718EEE1ABA637E2BE83D212CF71C0F755EC2` |
| Focused Debug results | 15,605 | 2026-08-14 21:23:36 | `73A9899971FF3940C70BBCB72EDEB17859929733150A0F81F55710B1E3B595DA` |
| Focused Release results | 15,765 | 2026-08-14 20:18:26 | `BFE04D0E1F2542E6790101E0F5038C105F8D3DD050B78A5EDFC309701533C1E1` |
| Full Debug results | 102,149 | 2026-08-14 21:02:32 | `790CC53A8414D4805006B90828EDFD8B1B0AC23667A16AA111B73DEE29667925` |
| Full Release results (3 identical) | 102,151 | 2026-08-14 20:35:57 | `1B0D66C13E778B9292C4C0889EA6952DF637EB50319BDE18FF1AFB90B2557978` |

## Known limitations

- Physical Android / iPhone / iPad runtime validation (touch hardware, safe
  areas, density scaling, system back gestures) is explicitly deferred to a
  later platform-port validation phase; the editor architecture and its
  platform-independent behavior are validated on Windows now.
- No third-party-host or external-format publication is claimed.
- Channel placement applies per band; the hosted ABI remains the permanent
  194-parameter surface and must not be reordered.
- Evidence is diagnostic grade (dirty shared workspace); a clean release
  commit/tag, transactional publication, and manual host validation remain
  later release gates.
