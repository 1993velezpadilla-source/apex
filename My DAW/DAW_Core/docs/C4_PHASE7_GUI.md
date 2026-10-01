# C4 Phase 7 — Final Console GUI (PASS)

Status: **C4 PHASE 7 — PASS** — declared 2026-08-13 after explicit user
visual approval (round 2). Phases 5 (sound) and 6 (analyzer) remain frozen.

## Architecture

`Source/C4UI/C4Editor.h` (header-only console UI) wired through
`C4Processor::hasEditor()/createEditor()` (the GUI include stays in the
.cpp TU — the DSP translation unit carries no GUI dependencies).

- **Vertical console architecture** (approved, frozen): four TALL VERTICAL
  CONSOLE STRIPS read top-to-bottom. Top utility row is supporting and
  quiet; the strips are the hero; BLOOM owns the bottom rail; the Spectrum
  Flag extends the window DOWNWARD.
- **Band identities**: WEIGHT bronze, SCULPT amber, BITE red-orange,
  OPEN ice-blue; BLOOM gold. Each strip is a channel module: identity
  header -> FREQ knob -> tall vertical GAIN fader (console ticks at
  +/-3/6/9/12/15 dB, emphasized 0 dB line) -> Q knob (SCULPT/BITE) or
  Bell/Shelf / Bell/HALO toggle (WEIGHT/OPEN).
- **Live gain dB readouts**: each fader draws its exact current value
  beside the thumb ("+6.5 dB", "0.0 dB", "-2.5 dB" — one decimal, explicit
  sign). The readout travels with the handle while dragging, updates on
  double-click reset, and follows host automation and state restore via the
  30 Hz editor refresh (the same parameter-text formatter the contract
  tests pin).
- **BLOOM hierarchy**: wide gold 0-10 fader in its own bottom rail with a
  soft glow and readout; labeled only "BLOOM" (never drive/saturation).
- **Spectrum Flag**: CLOSED/PRE/POST/BOTH. Opening expands the window from
  660x640 to 660x880 — the four strips keep their full 430 px height and
  every control (frequency, gain fader, dB readout, Q/mode) stays visible
  and interactive; the analyzer takes the space BELOW the BLOOM rail.
  Closing restores 660x640. Editor minimum host size 560x640 (the host can
  never shrink the console below its complete control set).
- **Analyzer presentation**: PRE silver / POST amber real spectra from the
  analyzer worker snapshot; the analytic total response curve (gold) is
  computed at 30 Hz from the engine's smoothed linear state — BLOOM
  harmonics are never drawn as a curve. Observational only: no draggable
  nodes, no grab, no auto-EQ, no G10 bells.
- **Performance**: 60 FPS repaint polling the snapshot; the FFT stays in
  the analyzer worker (~11 Hz); no FFT on the GUI thread; zero impact on
  the audio thread (Phase 6 contract re-verified).

## Editor lifecycle / integration

- Open/close/reopen cycles, multiple instances with independent editors and
  independent flag states, destruction order (editor before processor),
  state restore under an open editor, and mode toggles leaving all 19
  parameters bit-identical — pinned by `C4.EditorHook` (9 tests).
- Visual review harness: `C4.Preview` renders the review set from the real
  compiled editor and can open the live native editor window
  (`C4_PREVIEW_LIVE=1`). Fresh captures + `C4_PHASE7_REVIEW.html` live in
  `evidence/renderpacks/C4_PHASE7_PREVIEW/` (workspace archive) and the
  local review copy.

## Visual review rounds (user-directed)

1. Round 1: horizontal four-card dashboard REJECTED — user required a
   premium vertical console EQ (SSL/Harrison/Elysia ergonomics as reference,
   not copies). Redesigned into four tall vertical strips.
2. Round 2 (APPROVED): live gain dB readouts on every fader; Spectrum
   expansion must extend the window downward and NEVER shrink/remove the
   console controls. Both fixed with tests; the user granted visual
   approval of the vertical console as the frozen C4 direction.

## Regression evidence (final Phase 7 gate set)

| Gate | Result |
|---|---|
| APEX.C4 Debug (incl. editor hook + preview) | GREEN — `95b9bdeb11e1` |
| APEX.C4 Release | GREEN — `c3367ad2bae5` |
| Global Debug | GREEN — `c473aa0f3083` |
| Global Release | GREEN — `7d833c4211dc` |
| Repository policy | PASS |
| Dependency verification | PASS |

## DSP-frozen contract

No production tuning constant, BLOOM default, HALO mapping, coupling value,
residual setting, oversampling factor, or analyzer DSP contract changed
during Phase 7. All visual work is presentation-only.
