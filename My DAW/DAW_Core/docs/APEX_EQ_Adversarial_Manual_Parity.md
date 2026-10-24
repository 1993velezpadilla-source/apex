# APEX EQ — independent Pro-Q 4 workflow audit and adversarial acceptance

Reference: [FabFilter Pro-Q 4 official user manual](https://www.fabfilter.com/downloads/pdf/help/ffproq4-manual.pdf), **for behavior and interaction only**. All APEX code, logo and art must be original. Do not copy FabFilter copyright or claim proprietary algorithm equivalence.

## Verified from automated, executable behavior
- New native editor factory launches `ApexQ4Editor`, not a renamed instance of rejected Prism GUI.
- Audio chain uses stable APEX project parameter IDs and the already-reviewed realtime transition guard.
- `APEX EQ` heading, existing boot-eye rendering source, nonanimated.
- Rendered frequency graph with native host-controlled filter nodes, direct click/double-click, precision move, modifier creation, and selected-band context settings.
- Contextual HUD only appears when a band is selected; mobile prioritizes large ON, shape, HOLD SOLO, MORE and close targets, with advanced slope, channel and dynamics accessible through MORE.
- Real PRE/POST analyzer: test creates and decodes a **3-second stereo 48 kHz 16-bit WAV**, runs it through actual APEX EQ at **128 samples**, asserts finite FFT energy and positive processed-vs-original harmonic difference. Both spectrum curves share a calibrated dBFS ruler independent of the EQ gain scale.
- **Display-only** Gaussian log-frequency power integration at 1/36, 1/18 or 1/9 octave. Every drawn point is derived from genuine worker FFT bins; no fabricated harmonic content, no synthetic noise added to the screen. Tests reject any displayed dB peak greater than the highest measured raw bin.
- Native JUCE screenshots at 320×568, 480×800, 900×300, 1280×720, 1920×1080. Adversarial code checks panel containment, eye/preset separation, and pairwise overlapping contextual button hit regions.

## UNVERIFIED and not yet 1:1 (these must be addressed)
- Complete multi-select, grouping and proportional adjustment of EQ nodes.
- Drag interactions and gestures tested with actual Windows touch input rather than only synthesized JUCE mouse events.
- Dynamic band outer ring + threshold behavior, automatic dynamic threshold, full spectral mode controls, associated detector settings.
- All cut, shelf, bell, notch, all-pass, tilt slope cycling and full contextual drag/scroll behavior across channels.
- Analyzer freeze, peak hold, slope, collision checks, output metering, auto gain measurement.
- Phase-mode fidelity incl. natural/linear latency, oversampling, character modes and delay compensation.
- Full stereo/L/R/M/S placements, surround/channel layouts, crossover audition.
- Preset management + A/B, undo/redo history across host automation, Pro-Q instance workflows and EQ Match.
- Detailed visual reference comparison on actual screenshot: fonts, spacing, curve/node response, spectrum prominence and animation, menu density, touch margins.
- Apollo Solo **48 kHz / 128 samples** vocal song: uninterrupted moving bands, measured output, audible artifacts, host-save/reopen.

## Release gate
**Never merge to MAIN solely for a GREEN compiler or a resemblance claim.** Require focused MSVC/JUCE, EQ gauntlet Debug+Release, Windows complete suite, screenshot artifact review, and user approval. This new editor intentionally does NOT replicate FabFilter trade dress, source, DSP, icons or presets.

## PASS 7 — visual-only FFT freeze + measured peak hold (2026-10-23)

- **Freeze measured FFT:** available in the existing Analyzer menu only after an actual
  FFT snapshot exists. It keeps the last captured PRE/POST bins on screen,
  while the worker, audio thread, and independently measured dynamic-gain
  meter continue processing normally. The button displays `FFT FROZEN`;
  unfreezing fetches a fresh worker frame, not an invented interpolation.
- **Real per-bin peak hold:** optional dashed overlays for PRE and POST store
  the maximum **observed worker bin dBFS** since enabling the feature.
  They share the current analyzer's log-frequency power display smoothing
  and calibration. Clear resets only the local visual accumulators.
  Discontinuities and sample-rate/FFT-bin spacing changes invalidate old
  held peaks. Analyzer mode changes discard stale held frames.
- **Native WAV gauntlet extensions:** CI replays the same saved 48 kHz WAV
  through the actual EQ before and during freeze. It checks freeze frame
  identity, unchanged measured bins, dynamic processing while frozen,
  fresh-frame acquisition on resume, peak hold >= current true measured
  POST bin, and clearing/reseeding peaks. The full native screenshots
  remain real JUCE raster output.
- **No DSP/host state change:** freeze/peak hold are view-local, do not
  introduce realtime-thread allocations/locks, and do not claim a full
  Pro-Q 4 1:1 implementation. Pointer-based Windows touch tests, presets,
  visual approval, and actual Apollo Solo song playback remain release gates.

**CI state:** PASS 7 must be verified against its latest commit before it is
called GREEN. Keep PR #159 as DRAFT and MAIN untouched pending the gates.

## PASS 8 — touch-first linked EQ authoring (2026-10-23)

- **Explicit Touch Multi-Select mode** under the selected band's **MORE**
  contextual menu. A highlighted graph badge indicates when taps will
  LINK/UNLINK EQ handles. No desktop Ctrl/Cmd key is required.
- While link mode is active, a normal **tap** toggles band membership;
  a **swipe** on any linked node preserves membership and initiates
  normal begin/end host parameter gestures to edit group frequency
  (log-frequency ratios) and gain (dB differences). Unlinked swiped
  nodes join before movement; there is no accidental solo.
- An accidental tap on blank graph in this mode **never inserts** a
  new filter or destroys the current group. Exit from the mode clears
  links to all except the currently primary band so a later ordinary
  gesture will not unexpectedly move the previous group.
- **Dynamic ring dead-zone correction:** the painted outer GR arc extends
  to ~22.4 px; hit testing now accepts a 24px radius for enabled dynamic
  bands. The 13–24px ring drag zone is reachable in full without
  hijacking the centre drag. Static bands retain the old 22px hit target.
- Native JUCE adversarial tests run real callbacks at 320×568, 480×800,
  and 1280×720 with linked taps, blank-tap safety, grouped swipe,
  log-frequency/gain balance, one-step undo, unlink, 23px ring drag,
  and checks for accidental SOLO or unwanted gain/frequency changes.
- CI also exports `APEX-EQ-TOUCH-LINK-REAL-FFT-<size>.png` at five viewport
  sizes. These are **native JUCE captures** using the same actual processed
  48 kHz reference WAV PRE/POST FFT as the existing EQ screenshots, with
  the real grouped nodes and TOUCH LINK badge shown for design approval.
- Actual Windows precision-touch hardware remains an unverified release
  gate, and the native GUI still requires user visual acceptance. This
  is not proof of full feature or trade-dress parity with Pro-Q 4.

**PASS 8 CI:** verify latest SHA before claiming GREEN. Preserve PR #159 as
DRAFT, no MAIN merge until focused Windows/JUCE, Debug, Release, complete
suite, native screenshots and user acceptance pass.

## PASS 9 — desktop keyboard authoring and adaptive value tags (2026-10-23)

- EQ graph receives focus after a graph pointer-down (JUCE component focus),
  allowing hardware keyboard editing without synthetic Windows shortcuts.
- **Left/Right:** nudge each linked selected band's hosted frequency by
  ±1 semitone (multiplicative ratio, not linear Hz); **Shift+Left/Right:**
  ±0.1 semitone. **Up/Down:** ±1 dB; **Shift+Up/Down:** ±0.1 dB.
  Each key event balances host automation begin/end gestures per parameter.
- **Delete / Backspace:** remove all selected/link-group bands with one
  undoable history entry. Releasing an active audition before deletion
  prevents a stuck solo. Ctrl+Z / Ctrl+Y and Ctrl+Shift+Z: history, redo.
  **Escape:** exit touch link mode, deselect and close contextual panels.
  No band selected: arrow keys return unhandled so host can use them.
- Dynamic EQ value readout uses a graph-constrained 308px tooltip (vs
  static 224px), replacing a constant 214px width that clipped signed
  real-DSP DYN metering text. Contextual HUD collision scoring uses these
  same physical bounds.
- Adversarial native JUCE callback regression tests at 320×568, 480×800,
  1280×720 cover linked gain/frequency and fine-step musical intervals,
  one-step deletion undo/redo, escape safety, disabled-band behavior and
  tooltip bounds. Physical touchscreen and user GUI approval remain gates.
- Undo gap found by critic: preset **Reset to flat**, per-band context
  **Delete**, **Reset**, and **Shape/Slope/Placement** popup mutations
  previously omitted history commits. Each now commits after applying real
  hosted values; preset reset balances Enabled host gestures for all active
  bands and is reversible with a single Undo. Tests verify true reset/restore.


**PASS 9 CI:** latest SHA must pass focused Windows/JUCE compiler, Windows
complete tests, both Parametric EQ Debug/Release and Flagship EQ Gauntlet,
and repository checks. Do not merge PR #159 into MAIN from older GREEN.


## PASS 10 — screenshot-driven dynamic panel occlusion fix (2026-10-23)

Actual `APEX-EQ-DYNAMIC-WAV-FFT-480x800.png` downloaded from PASS 9
Release run 39990858105 exposes a **severe visual defect** that previous
geometry/green CI missed: dynamic slider values and four rows visibly
paint *over* the floating FREQ/GAIN/Q HUD and SOLO. The screenshot at
1280x720 does not show this failure, proving a desktop-only smoke test
would miss it.

- Replace the 4-corner-only floating dynamic-panel placement solver with
  an allocation-free 3×9 candidate search (corners, central positions,
  just above/below the selected band and existing contextual HUD).
- Penalize actual intersecting **area** with the primary band controls,
  value tooltip and hosted band-node points instead of assigning
  insufficient constant collision penalties. Preserve enabled nodes
  and signed dynamic metering visibility.
- Under 460px wide, there is inadequate room for two large floating
  overlays simultaneously. Opening Dynamics temporarily hides the
  FREQ/GAIN/Q HUD and its input widgets. Closing Dynamics restores
  normal band editing without changing the DSP/parameters.
- Additional critic finding on the 900×300 landscape layout: there is
  insufficient vertical room to avoid the signed DYN tooltip with a
  346px wide card. The card narrows to 260px ONLY at landscape host
  heights <260px canvas, retaining >160px dynamic slider hit widths
  and avoiding the live tooltip when parked on the opposite side.
  Native gauntlet now explicitly checks tooltip and selected-node access.
- Expand the native JUCE gauntlet to assert no Dynamic/HUD overlap
  at 480×800, 900×300 and 1280×720. Test compact HUD substitution and
  restoration at 320×568. Generate **real** `APEX-EQ-DYNAMIC-WAV-FFT`
  images at all five viewport sizes to allow direct human visual review.
- The review uses genuine prior PNGs, not generated mockups; release CI
  on the latest HEAD and examination of NEW native screenshots remain
  pending. No claim of commercial product visual parity is made.

Keep PR #159 in DRAFT; do not merge MAIN without all latest-head CI
GREEN, visually confirmed screenshots and explicit user approval.

### PASS 10 adversarial CI failure and corrective iteration (2026-10-23)

Commit `52938141` compiled on Windows but **FAILED** the Windows Complete
Tests and Debug/Release Flagship EQ Gauntlet. Do not represent this run as GREEN.

Actual test diagnostics from run 40025466359:
- Five `all contextual hit targets fit entirely in floating HUD`
  assertions failed; root cause: the PASS 10 dynamics layout helper called
  `setSelectedVisible(true)` and re-enabled mobile-only hidden `slope_`,
  `placement_` or `dynamic_` controls whose inactive desktop rectangles
  were outside the compact HUD. Fix: force per-breakpoint visibility inside
  `setSelectedVisible` after applying global selection visibility.
- One `dynamic panel must NOT obscure ANY part of the EQ knobs or SOLO`
  assertion failed; screenshot review and geometry audit show short 900×300
  windows cannot always place two overlays without collision for every
  possible band point. Fix: treat short landscape windows the same as compact
  portrait, temporarily substituting the dynamic editing card for the
  selected-band controls. Close restores FREQ/GAIN/Q. At ordinary desktop/
  mobile heights, **both remain visible** with zero-area overlap.
- The tests now explicitly distinguish visibility substitution from a
  collision, and forbid substitution at normal-height responsive widths.

These failures are genuine and documented; the corrective HEAD still
requires an entirely new full GitHub Actions GREEN. MAIN remains untouched.

### PASS 10 targeted regression strengthening — Dynamics OPEN → CLOSE

Post-correction adversarial test now repeats the full native responsive
geometry check **after** the Dynamic EQ panel is opened then closed for
320×568, 480×800, 900×300 and 1280×720. Every *visible* contextual
button must be entirely inside the selected-band HUD, and the narrow
breakpoints must keep Slope / Placement / Dynamic buttons hidden according
to design. This directly guards the five original PASS 10 CI failures
against a future visibility regression. All latest-SHA Windows CI gates,
real native PNG review and explicit user approval remain required.


## PASS 11 — contextual band popover follows the active EQ node (2026-10-23)

**Real screenshot visual finding:** PASS 10 native Release PNGs at 480×800
and 1280×720 show the active-band FREQ/GAIN/Q popover docked near the
bottom of the canvas, conspicuously separated from the selected EQ handle.
The GUI should behave as a small contextual control rather than a bottom
sheet, and the selected band must remain understandable during live audio.

- Replace old `(maxY - y) * 1.7` bottom-docking preference with selection-
  anchored placement. Prefer 49 px **below** the selected node, then above
  with a small bias when the lower position is blocked or constrained.
  Compare candidate positions near the node as well as safe edges.
- Retain strict band handle and value-tag occlusion penalties; full-width
  frequency graph remains intact. No new DSP allocation or audio work.
- On 460–669px medium phones **while Dynamics is expanded**, deliberately
  favor a lower band HUD so both separate cards have room. Closing the
  Dynamics panel resumes the node-following behavior.
- Add native JUCE callback/geometry tests at 480×800, 640×400,
  1280×720 and 1920×1080 for both EQ node selections. Assert the
  contextual HUD remains within 210px of the selected node, covers no
  active node or value tag, and remains disjoint from expanded Dynamics.

**Not yet certified:** CI must execute on PASS 11 HEAD and the new native
WAV-based GUI screenshots must be visually reviewed. No claim of Pro-Q 4
visual or feature 1:1 parity; PR #159 stays DRAFT and MAIN untouched.


### PASS 11 failed CI (Oct 24 UTC) and exact 640×400 remediation

Latest original PASS 11 commit `cf982d3` compiled and passed policy
checks, but **failed** both Flagship EQ Debug/Release and full Windows
test suite (runs 40048693044, 40048693189). All failing jobs agreed:
`near-node HUD must not hide selected value tooltip`.

The repro is **640×400, band #1 (5200 Hz, -5 dB)**. The old band
popover reserves an unnecessary 29px above the footer, clamping its
lowest candidate to y=211 where it intersects the selected value tag
(y≈208–230). The popover's alternate high position obscures another
enabled EQ node. The candidate scorer therefore chose the *less*
penalized tooltip collision, correctly caught by the new adversarial test.

- Keep full 29px reserve for ordinary-height windows. At viewport height
  <=420px, use an 8px reserve from the graph bottom; the footer remains
  outside the canvas. At 640×400 this permits y=232, which sits below the
  selected value tag and leaves 8px below the 116px contextual panel.
- Pin a targeted regression for the exact 640×400, selected band #1
  case, checking the readout, graph bounds and 8px footer clearance.
  Include the viewport/band name in future failing assertions.
- **Do not claim success** until all four CI workflows complete GREEN on
  the latest commit, followed by visual verification of real screenshots.
  PR #159 remains DRAFT, MAIN unmodified.


## PASS 12 — genuine FFT display domain versus unmeasured 30 kHz (2026-10-24)

**Visual finding in native PASS 11 Release PNG:** the PRE/POST FFT fills
formed a conspicuous right-hand slanted wedge / cliff at the far edge of
the EQ graph. The GUI frequency canvas correctly spans 20–30,000 Hz,
but the WAV input is **48 kHz sampled**, so worker FFT data ends below
24 kHz. Prior `drawSpectrum` correctly ignored invalid bins for its
line stroke but closed the filled path to `x = getWidth()` (30 kHz),
painting a false triangle over frequencies where audio wasn't measured.
This is a visual integrity defect, not a DSP bug.

- A single derived, native-frequency `measuredSpectrumDomainX()` takes
  the actual FFT bin spacing and last worker bin into account, capped to
  the graph maximum of 30 kHz.
- PRE and POST strokes, held-peak overlays and semi-transparent FFT fill
  now integrate only over that measured domain. A filled spectrum closes
  vertically at the *last successfully plotted real bin* rather than
  extrapolating to either artificial x-boundary.
- Tests use genuine 48 kHz WAV-derived worker FFT frames and assert the
  displayed domain stops inside the 30 kHz canvas (about 97% of the
  width) for desktop and mobile. An unmeasured 29 kHz reading must remain
  at the no-data floor, not contain invented energy.
- Both DSP and host parameter automation are completely unchanged.
  PASS 11 was GREEN on HEAD `46b207b9`, but PASS 12 **MUST NOT**
  be marked GREEN without its *own* four-workflow CI and native
  screenshot review.

PR #159 remains DRAFT; do not merge MAIN without user approval.


## PASS 13 — real screenshot color hierarchy and analyzer provenance (2026-10-24)

Adversarial critic inspected **actual PASS 12 Release native JUCE PNGs**,
including the 1280×720 and 480×800 real WAV spectra. Although PASS 12
was fully GREEN, its dense *POST FFT* was drawn warm yellow, almost
indistinguishable from the equally yellow overall EQ transfer function.
The screenshots make it hard to know whether a peak is measured incoming/
outgoing audio or the frequency response of a user-adjusted filter.
The selected band nodes are individually colored, but their FREQ/GAIN/Q
rotary rings remained yellow too, further weakening visual hierarchy.

### Changes, deliberately restricted to GUI paint and selection styling

1. Keep the **overall EQ response in gold** (warm), while displaying true
   audio FFT **PRE in muted lilac** and **POST in cool teal**. Peak-hold
   dashed traces inherit their corresponding measured audio color.
   Reduce FFT fill opacity so the actual response remains legible under
   dense harmonic material; do not synthesize/massage analyzer values.
2. Add a compact PRE/POST key inside the top-left of the graph. Only
   draw entries for *actual* active real worker data. The legend is not
   displayed for a missing or disabled analyzer, and it never implies
   spectral data when the audio worker has none.
3. Match FREQ/GAIN/Q knob rings to the selected **band node's own color**.
   Changing band selection updates all three rings while the global
   response retains its separate gold identity.
4. Extend the native 48 kHz PCM→EQ→worker FFT gauntlet at 320×568,
   480×800 and 1280×720: verify real PRE/POST measurement is present,
   color identities are visibly distinct, the legend fits the graph,
   and POST is teal while the EQ response stays warm. Additional
   breakpoint/selection geometry tests assert that the three band dials
   agree with each other, differ from the gold response, and switch
   accents whenever the selected band changes.
5. Existing screenshots are produced **natively** by the Windows JUCE
   gauntlet from real WAV audio; nothing is generated or faked. Compare
   the PASS 13 screenshot artifacts visually to PASS 12 after CI.

**No DSP filters, parameter IDs, automation storage, threading or audio
calculations changed.** No visual 1:1 parity claim; keep PR #159 DRAFT
and MAIN unchanged until all latest-head CI checks and native image
inspection are complete, plus explicit user approval.


## PASS 14 — FabFilter Pro-Q 4 Dynamic EQ band-controls workflow (2026-10-24)

**Requested reference:** https://www.fabfilter.com/help/pro-q/using/dynamic-eq
and the user-provided zoomed official manual image (curved floating band
card containing FREQ, GAIN, Q, gain-range annulus, threshold bar, key
trigger, attack and release controls). **Strict source discipline:** model
the user-facing interaction with wholly APEX-owned C++/JUCE components,
not copied FabFilter artwork, logos, proprietary DSP or fonts.

### Committed to feature branch, user-visible source behavior

- Introduce `ApexQ4DynamicGainDial`, a real JUCE Slider subtype; the
  **outer annulus** around GAIN has a separate drag hit test from the
  **inner static gain knob**. Drag writes actual signed dB Range through
  `beginChangeGesture`/`setValueNotifyingHost`/`endChangeGesture`,
  automatically activating hosted Dynamic Enable only after moving away
  from zero. Handles pointer-up, selection change and destruction
  without leaving automation gestures dangling.
- The outer ring expresses the actual signed per-band hosted range as
  red compression or magenta expansion. A small gold inner mark shows
  the **last coherent audio-thread measured dynamic gain** from
  `ResponseFrame`, not generated animation. No measured frame means
  no gold gain-reduction mark. Ring is present only for Bell/Low Shelf/
  High Shelf/Flat Tilt, not cut, notch or phase-only filters.
- Preserve existing native threshold/range/attack/release sliders in
  contextual advanced Dynamics panel and the genuine DSP-driven control
  values; accessible through MORE on very narrow touch viewports.
- Make newly created Dynamic EQ bands (Alt-create, Alt-wheel activate,
  or first gain-ring drag) default to a **band-limited internal trigger**;
  this changes only the explicit new-band activation path, **not**
  existing session migration/default legacy source settings.
- Replace misleading Filter toggle with a **real trigger selector**
  (Band-limited linked key vs Full-band/wideband key), plus
  **Internal vs External Sidechain** options using the existing
  *global shared* host-sidechain parameter. It is explicitly labelled
  **SHARED ACROSS BANDS**, not claimed as per-band FabFilter behavior.
  A reset action clears per-band Dynamic Range to zero and disables
  Dynamic Enable with balanced host automation notifications; it does
  not silently alter static frequency/gain/Q.
- New JUCE tests at 320×568, 480×800, 900×300 and 1280×720 verify
  ring hit geometry, independent static vs dynamic knob gestures,
  negative dB range, newly enabled band key, no accidental audition,
  disallowed shapes, clear/reset, and GUI ring meter fidelity to a
  *real* PCM→DSP ResponseFrame. Run all existing flagship EQ and Windows
  integration tests after the pass.

### Adversarial parity gap matrix — do NOT call this 1:1 yet

| Manual behavior | APEX status after PASS 14 |
| --- | --- |
| Dynamic range ring around GAIN; independent static gain | **Implemented** with actual hosted automation and touch |
| Signed range, compression vs expansion | **Implemented** in APEX DSP limit **±24 dB** |
| Yellow current reduction inside ring | **Implemented** from actual coherent DSP frame |
| Band-limited default trigger for newly dynamic bands | **Implemented** without rewriting saved sessions |
| Trigger on band or wideband, internal/external source | **Implemented**, external source **shared global** |
| Threshold/attack/release custom control | Existing hosted DSP and editable advanced panel |
| Reset range to static band | **Implemented** using existing host parameter notifications |
| FabFilter ±30 dB exact range | **NOT implemented**. APEX saved normalized automation and DSP range are ±24 dB; changing the old host parameter ABI requires explicit compatibility and regression work |
| FabFilter adaptive AUTO threshold/attack/release | **NOT implemented**. APEX has user-adjustable fixed threshold and time parameters; labelling fixed behavior AUTO would mislead users |
| FabFilter editable *Free* high/low trigger cutoff frequencies | **NOT implemented**. APEX only has bandpass-linked or unfiltered wideband trigger |
| FabFilter per-band external sidechain source | **NOT implemented**. APEX sidechain source is one shared parameter |
| Reproduction of exact original widget artwork/shape | **NOT claimed**. UX-inspired original APEX skin, responsive geometry |

**Critical:** The new commits are not GREEN merely because PASS 13 was
GREEN. The exact PASS 14 HEAD needs new debug/release EQ, Windows
integration, focused compile and policy checks and real WAV-native
screenshots. No MAIN changes; PR #159 remains DRAFT pending approval.


### PASS 14-B — signed dynamic controls & close-to-reference gestures

The original DSP in `ParametricEQTypes.h` explicitly defines
**hosted** `rangeDb > 0` as cutting above threshold, and
`rangeDb < 0` as boosting below threshold. The Pro-Q 4 manual uses
the opposite **visible UI signed range** convention: *negative UI*
for compression, *positive UI* for expansion.

To reproduce familiar **editing behavior without destroying existing
APEX automation**, the GAIN annulus now maps
`displayedDynamicRangeDb = -hostedRangeDb`. Dragging the GAIN
outer annulus writes the inverse into the untouched hosted parameter.
The explicit RANGE slider in the expanded Dynamics editor also uses
`uiNormalized = 1 - hostedNormalized`, including text entry,
dragging and parameter synchronisation. All old saved host values and
DSP mapping stay unchanged; unit tests prove that UI -7 dB corresponds
to legacy host +7 dB, while typed UI +7.5 dB corresponds to legacy
host -7.5 dB. This is only a GUI-adapter mapping, not a DSP
compatibility rewrite.

The GAIN outer ring additionally handles mouse wheel, Shift+wheel for
fine adjustment, and double-click to reset; its signed dB readout is
drawn alongside the existing measured dynamic gain ring.
The Dynamic EQ advanced panel gains a genuine RESET button when >=330px
wide, with a popup-menu reset path for narrow touch layouts.
Screenshots must be inspected for actual hitbox sizes and occlusion
**after** all four HEAD-specific CI workflows complete.


### PASS 14-C — independent adversarial CI follow-up and integrated workflow

**First full PASS 14 CI on HEAD `cd42f744` was RED**:
repository checks GREEN but focused JUCE compile, Windows build,
Debug EQ and Release EQ all failed from the same MSVC `C3535` at
`ApexQ4Editor.h:1965`: `std::initializer_list` cannot infer a
common type from `&freq_`, `&gain_` (now a derived
`ApexQ4DynamicGainDial`), and `&q_`. The precise compile correction
uses `std::array<juce::Slider*, 3>` with the GAIN dial explicitly
cast to its parent Slider type. **Focused compile** on corrective
commit `aec273fa` and `9e7a7a88` was GREEN. Superseded full
test jobs were cancelled by later pushes (not claimed green).

A second independent source audit caught inverted-range conversion
incorrectly omitted for the advanced RANGE **numeric text input**.
That is now mapped in both directions: UI-text <-> UI normalized
value <-> original hosted normalized value. Typed +7.5 dB expansion
must produce host -7.5 dB while preserving frozen plugin state.

Another GUI-only pass adds **attached-stack placement** on roomy desktop
hosts: the advanced dynamic card is preferentially immediately below
(or above) the selected band's main FREQ/GAIN/Q/Gain-Range card.
The established anti-overlap solver still takes priority over this
visual preference, so graph nodes, SOLO, numeric boxes and the
selected value tag remain unobscured. Phones retain their original
safe responsive mode. This improves continuity but is **NOT** yet
the manual's exact single curved widget.

Interaction completeness: Alt-wheel refuses to create dynamics on cut
and non-gain bands, and disables Dynamics at zero range. The selected
node's outer arc now matches the signed red-compression/magenta-expansion
GAIN annulus. **Clear Dynamics** now applies to *all selected linked
bands* as a single undoable host-automated operation, and regression
tests assert both reset and restore correctly.

**Audit state:** latest head needs NEW four-workflow Windows CI and
actual unmodified native JUCE screenshots; do NOT declare PASS 14 green
or visual 1:1 prematurely. APEX saved session ABI and audio DSP remain
unchanged; PR #159 DRAFT / MAIN untouched.


## PASS 15 — one physical floating desktop Dynamic EQ band card (2026-10-24)

**Reference:** user-provided official Pro-Q 4 Dynamic EQ screenshot, with the
band's shape controls, frequency, main/dynamic GAIN ring, Q, detector,
threshold, attack/release and numeric values in one floating surface.
Previous PASS 14 Release *native* 1280×720 WAV screenshot was GREEN
functionally but revealed a large, detached 346×272 dynamics sheet.

**APEX-owned clean-room JUCE change:**
- Only in genuinely roomy desktop editors (width >=1280 and graph height
  >=460), opening Dynamic EQ now creates ONE shared **804×180** curved
  floating container. The existing 450px band-controls row sits at its
  left, and the real hosted dynamic controls live on its right. The
  8px inter-component gutter receives a single continuous painted
  background, outline and subtle seam rather than two separate cards.
  No widgets or labels are rasterised/static screenshots; all stay
  real JUCE children with actual host automation, numeric input and
  keyboard/mouse/touch access.
- The whole wide surface is positioned **as a unit**, rather than moving
  two unrelated windows. The deterministic candidate search penalises
  overlap with all enabled EQ node handles and the selected band's
  numeric value tag and clips the full surface inside the graph. It
  prefers below or above the selected band.
- Paint draws a single original APEX purple/dark gradient card with
  softly rounded border, selected-band accent and separator. It no
  longer draws two floating rounded rectangles while unified mode is
  active. Its decorative padding blocks accidental click-through band
  creation.
- Short desktop landscapes and phones deliberately retain the
  preexisting collision-tested separate/compact Dynamics editor;
  forcing an 804px control bar into 320px would break touch accessibility.
- Geometry and touch adversarial tests assert the WHOLE control surface
  sits inside the frequency graph, both subcomponent zones belong to
  it, the united height is 180px (vs prior detached 272px), graph
  nodes/selected tag remain touchable, and tapping painted padding
  cannot create EQ nodes.

**Limits:** Although the desktop presentation is now a physically
single floating surface, the individual APEX controls are not pixel
identical to proprietary FabFilter artwork. The old automation ABI
supports ±24dB, not ±30dB; Auto adaptive behavior, Free detector
cutoffs, per-band external source remain unimplemented. DSP untouched.
**Latest PASS 15 HEAD requires four fresh CI checks and real Windows
JUCE screenshot review before saying GREEN.** PR #159 DRAFT; MAIN untouched.
