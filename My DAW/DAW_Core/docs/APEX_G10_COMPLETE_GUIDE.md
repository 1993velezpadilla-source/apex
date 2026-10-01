# APEX G10
## Complete User Guide & Under-the-Hood Technical Reference
### Windows v1.0 Final

---

**Product:** APEX G10 — 10-band musical EQ with a Mini Clean EQ layer, analyzer, input/output metering and a fixed character stage.

**Frozen source:** tag `g10-windows-v1.0-final` (commit `df319fd5410286b0318eddac3e07281d708921e6`).

**How to read this document:** Part I through Part VIII are written for a first-time user. Part IX onward progressively deepens into signal flow, DSP internals, parameters, state, realtime safety and engineering architecture. Implementation references point at the frozen source (`Source/G10Core/…`, `Source/G10UI/…`, `Source/EQCore/…`); the source is the final authority wherever this document and older documentation differ.

---

# PART I — WHAT IS G10?

APEX G10 is one plugin built from **two deliberately different EQ responsibilities** working in the same window:

1. **A musical 10-band tone-shaping core.** Ten fixed bands — 31 Hz to 16 kHz — each with its own musical character. These faders are for *decisions about the music*: make it deeper, punchier, warmer, clearer, brighter. Each band is a designed *curve family*, not a plain parametric bell; small movements are broad and forgiving, larger movements get more focused, and boost behaves differently from cut in ways that match the band's musical job.
2. **A small clean surgical EQ layer (the "Mini Clean EQ").** One HPF, one LPF and up to **three** Bell nodes on the analyzer graph. This layer is deliberately the *opposite* personality: transparent, precise, mathematically predictable. It is for *corrections*: removing a resonance, filtering rumble, taming a harsh frequency — the kind of work that should be exact and invisible.

Why two layers instead of one? Because the two jobs fight each other if forced into one control set. A musical band that "sounds great" when you push it is usually *not* the right tool for a surgical −6 dB notch at exactly 3.17 kHz — and a surgical parametric is usually the wrong tool for "make the kick drum warmer." G10 gives each job its own controls, so you never compromise one to serve the other.

Between them sits the **analyzer**: a live spectrum display with the Mini EQ response curve drawn on it, joining both worlds visually. You shape music with the faders, see what the signal is doing in real time, and place precise corrections where the spectrum shows a problem.

Two compact **input/output level meters** complete the workflow: gain staging before and after processing.

That is the whole mental model:

```
10 musical faders     ->  tone / color / broad musical decisions
Mini Clean EQ         ->  precise correction / resonances / filtering
Analyzer              ->  visual feedback joining both worlds
Input/Output meters   ->  gain staging before and after processing
```

G10 is not an emulation of any specific hardware unit, and it does not claim to "sound better" than anything else. It is a defined instrument with a defined character (see Part XII–XIV for exactly what that character is).

---

# PART II — QUICK START FOR A NEW USER

If you have never seen G10, here is a sensible first session. You do not need to understand any of the engineering in this book to get good results.

1. **Insert G10** on a track (or a bus). The plugin loads with everything flat: all ten bands at 0 dB, Input/Output at 0 dB, Mini EQ off.
2. **Watch the Input meter.** It shows the level entering G10's processing (after the Input trim). If the track is very hot or very quiet, this is where you see it.
3. **Adjust the Input knob if needed.** It ranges ±18 dB and is there to get the signal into a comfortable working zone. (The character stage is level-sensitive; see Part XII.)
4. **Shape the tone with the ten musical bands.** Start broad: try the 31 Hz and 63 Hz faders for low-end weight, 2 kHz for attack, 8 kHz for shine. Move them by small amounts — the bands are designed so small moves sound like *the music changed*, not like a filter sweep. Drag a fader, or hover and use the mouse wheel (0.1 dB per notch, Shift for 0.025 dB fine steps).
5. **Use the analyzer to find anything that needs precision.** Play the material and watch the spectrum. A persistent bump around 300 Hz that sounds boxy, a harsh peak around 3 kHz — the analyzer shows you where the energy is.
6. **Double-click the analyzer graph** where the problem is. G10 creates a Bell node at exactly that frequency and gain (X = frequency, Y = gain). The new Bell is enabled, not bypassed, with a default Q of 1.0.
7. **Adjust the Bell.** Drag it: horizontal = frequency, vertical = gain. Hover over it and roll the wheel (or Ctrl + vertical drag) for Q. Double-click a value in the floating inspector to type exact numbers. If you need more precision, hold Shift while dragging.
8. **Use the HPF and LPF where needed.** Click the HPF node at the left edge of the graph (or drag it) to roll off low rumble; the LPF at the right edge for harsh highs. Both cover the full 20 Hz–20 kHz range and can be switched fully off.
9. **A/B individual Bells.** Alt-click a Bell (or use its Bypass button / right-click menu) to hear the track with and without that one correction — without losing the settings. That is *bypass*, which is different from *delete* (Part V).
10. **Watch the Output meter.** It shows the exact final level leaving G10 (after everything, including Bypass). If your boosts add up hot, this is where you see it.
11. **Use the Output knob for level matching.** When you A/B G10 against the dry signal, match the loudness with Output so you judge tone, not level.
12. **Compare with the global Bypass.** The BYPASS button bypasses the whole plugin with a smooth crossfade. It is the definitive "what did G10 actually do to this track?" check.

That is a complete first session. Everything else in the front panel is refinement of these steps.

---

# PART III — COMPLETE FRONT-PANEL TOUR

The G10 editor is a 1000×612 logical design (scaled 0.625 from the approved 1600×980 blueprint): header with logo/title, analyzer, ten faders, control strip.

## INPUT (top-left of the control strip)

- **Input knob** — the input trim.
  - Range: **−18 dB … +18 dB**, default **0 dB**.
  - Purpose: gain staging *before* internal processing. It is a linear gain applied to the signal entering the musical core, smoothed over ~15 ms. It also feeds the character stage, so it is the "how hard does G10's color get hit" control.
- **Input meter** — a compact vertical peak meter (48×92 logical px) beside the knob.
  - Exactly what it represents: **the real signal entering G10's internal processing, measured after the Input trim** (the trim is a linear gain, so its dB value simply adds to the measured peak). Measured once per host block on the audio thread, displayed by the editor's 30 Hz timer.

## The ten G10 bands

Each band is a fader column (92 px wide) with a stylized "eye" handle, a frequency label, a musical name and a live gain readout. All ten share the same product contract: **center frequency fixed, range −12 dB … +12 dB, default 0 dB**, smoothing ~20 ms.

| Band | Frequency | Musical name | Character (from the frozen design) | What boosting does | What cutting does | Example uses |
|---|---|---|---|---|---|---|
| 1 | 31 Hz | **DEEP** | Ultra-wide low bell + very broad low shelf (compound) | Sub/body foundation grows; wide, not lumpy | Cleans mud below ~50 Hz; broad low shelf emphasis | Kick weight, sub-bass foundation, removing low mud |
| 2 | 63 Hz | **PUNCH** | Proportional-Q bell with a very subtle supporting contour near 120 Hz | Punch and low-mid slam | Tames boxy low-mid energy | Kick/floor-tom punch, bass body |
| 3 | 125 Hz | **BODY** | Hybrid bell/shelf: boost 70/30, cut 83/17 with a low shelf anchor near 110 Hz | Adds low-mid body | Thins/shortens the low-mid, de-muddies | Guitars, vocals' chest, low-mid clutter control |
| 4 | 250 Hz | **WARMTH** | Bell whose boost is intentionally *broader* than its cut | Warm, round fullness | Tighter reduction of congested low-mids | Vocal warmth, boxiness control |
| 5 | 500 Hz | **WOOD** | Bell whose boost is *more focused* than its cut | Focused low-mid presence ("wood" of instruments) | Broad reduction of honk | Snare/woodwinds, honk removal |
| 6 | 1 kHz | **FOCUS** | Pure proportional-Q bell, mid Q | Center/focus of the source becomes clearer | Removes mid-range forwardness | Vocals' presence, mid carving |
| 7 | 2 kHz | **ATTACK** | Proportional-Q bell, high Q growth | Attack/articulation of transients | Softens aggressive upper mids | Drums' attack, vocal edge |
| 8 | 4 kHz | **PRESENCE** | The most focused band; boost tighter than cut | "In-front" presence | Reduces harshness forwardness | Vocal presence, harshness taming |
| 9 | 8 kHz | **SHINE** | Continuous broad bell ↔ high-shelf morph (see below) | Shine/air on top without a narrow ring | Gentle de-gloss (broad bell darkening with a small shelf component) | Cymbals, vocal air, de-essing support |
| 10 | 16 kHz | **AIR** | Exact high shelf with sample-rate-adapted turnover | Air/space, smooth top | Darkening/softening of the very top | High-end air, softening brittle tops |

**Broad curve philosophy (documented in the source, `G10BandEngine::qLaw` / `recomputeCoefficients`):** the bands follow a *proportional-Q principle* — small movements are broad and forgiving, larger movements progressively more focused, never surgical. Boost and cut intentionally diverge where the family contract demands it (e.g., 250 Hz boost broader than cut; 500 Hz and 4 kHz boosts tighter than their cuts). Shine's bell morphs continuously toward a high shelf as you boost (≈40% shelf at +6 dB, predominantly shelf near +12 dB), so brightening never rings like a narrow bell; Air is an exact RBJ high shelf with the turnover one octave below the 16 kHz anchor (adapted with sample rate) so the anchor region reaches the asymptotic shelf gain.

The source also records an *internal research lineage* (`G10Types.h`) naming the classic mixers/consoles whose *philosophy* inspired each family (e.g., Deep31 — Pultec EQP-1A philosophy, Punch63 — SSL/API philosophy, Body125 — Neve 1073 philosophy, Air16k — Maag Air Band philosophy). **This is design inspiration, not hardware emulation** — G10 implements its own DSP, and none of its curves are copies of any hardware unit's circuit.

## Fader interactions

- **Drag** the eye handle vertically to set gain; the value only edits after the pointer actually moves (a plain click changes nothing).
- **Mouse wheel** over a fader: **0.1 dB per notch**; hold **Shift**: 0.025 dB fine steps. One wheel event is one complete, balanced host edit.
- **Double-click a fader → exact 0 dB reset.** This is a protected interaction contract (`G10BandFaderComponent::mouseDoubleClick`): the second click is deliberately *not* treated as a drag start, the fader resets to the product's exact unity value (normalized midpoint = 0 dB), and the reset is a proper host gesture (begin → set → end). Double-clicking an already-zero fader changes nothing.

## The control strip (center)

- **BYPASS** — the global bypass button (see Part IX).
- **OUTPUT knob + Output meter** — mirror of the Input pair on the right side. Output trim range **−18 dB … +18 dB**, default 0 dB. The Output meter shows **the exact final buffer delivered to the host** — measured after everything, including the bypass crossfade.

## The analyzer

The large central panel: live spectrum (140 log-spaced bars, 20 Hz–20 kHz), a dB grid (0 … −60 dB guide lines), the Mini EQ response curve, and the interactive nodes (HPF at the left edge, LPF at the right edge, up to three Bell nodes in between). Full behavior in Parts IV–VII.

---

# PART IV — MINI CLEAN EQ

The Mini Clean EQ is the surgical layer. Structure, fixed and hard-limited:

```
HPF (2nd-order Butterworth, 12 dB/oct, Q = 0.70710678)
B1  (clean peaking Bell)
B2  (clean peaking Bell)
B3  (clean peaking Bell)
LPF (2nd-order Butterworth, 12 dB/oct, Q = 0.70710678)
```

Cascade order is exactly **HPF → B1 → B2 → B3 → LPF**.

## Why the 3-Bell limit is intentional

G10's identity is "ten musical faders + a *small* surgical layer." Three Bells cover the realistic correction workload (one resonance, one harshness, one general carve) while keeping the layer surgical by design — the product deliberately does *not* grow into an unlimited parametric EQ (see Part XXI). The limit is enforced in one place (`ApexInteractiveEQSurface::mouseDoubleClick`: `"3/3 Bells active"` when full), and a fourth Bell cannot be created by any path.

## HPF

- Frequency range: **20 Hz … 20 kHz**, logarithmic control law.
- **OFF behavior:** the HPF has a *true OFF endpoint* (normalized 0). OFF is not a fake "filter at 1 Hz"; the stage is genuinely out of circuit (bit-identical pass-through once settled).
- Topology/order: **2nd-order Butterworth (Q = 0.70710678), 12 dB/oct**, realized as a TPT (topology-preserving transform) state-variable filter — the same primitive the musical core uses.
- **Minimum-phase:** yes — an IIR filter with no lookahead; zero added latency.

## LPF

- Frequency range: **20 Hz … 20 kHz**, same log law as the HPF.
- **OFF behavior:** true OFF endpoint at the top of the range (normalized 1). No fake "filter at 20 kHz".
- Topology/order: identical to the HPF — **2nd-order Butterworth, 12 dB/oct, TPT SVF**, minimum-phase.

## Bells B1/B2/B3

- **Frequency: 20 Hz → 20 kHz** (logarithmic).
- **Gain: −12 dB → +12 dB.**
- **Q: 0.10 → 40.0** (logarithmic).

## What Q means in human language

Q describes how *wide* the bell's influence is around its frequency:

- **Q ≈ 0.10** — extremely broad: the bell touches everything around it; a slow tonal movement rather than a specific frequency. Useful for "this whole region feels off by a bit."
- **Q ≈ 1.0** — the normal musical bell: a few semitones wide, natural-sounding.
- **Q ≈ 10** — narrow: only a small band around the frequency is affected; good for taming a specific resonance.
- **Q ≈ 40** — surgical: a razor-thin notch or peak; the tool for hunting a single resonance. At this Q the visible curve looks almost like a spike — that is correct, and the curve is guaranteed to pass exactly through the bell's center (Part VII).

**Clean personality:** the Mini EQ is completely transparent, minimum-phase, mathematically defined — no saturation, no resonance bumps, no proportional-Q musicality. It is intentionally the opposite of the musical core.

---

# PART V — EVERY MINI EQ INTERACTION

## Double-click empty graph → create a Bell

Double-click anywhere on empty analyzer space. G10 creates the first available Bell **exactly at the clicked position** — X maps to frequency, Y maps to gain (the same plot geometry as the graph). The new Bell is:
- enabled,
- **not bypassed**,
- at **default Q = 1.0**,
- immediately selected (its inspector opens).

If all three slots are active, G10 shows a brief status message instead: *"3/3 Bells active"*.

*(Double-clicking a Bell node itself is different: it resets that Bell's gain to 0 dB while keeping frequency and Q — the same command path as the panel's Reset Gain. Double-clicking the HPF or LPF handle switches that filter fully OFF.)*

## Dragging a Bell

- **Horizontal drag = frequency.**
- **Vertical drag = gain.**
- The node moves, the response curve moves, and the inspector follows the node.
- **Shift** = fine control (drag sensitivity ×0.08).

## Q

- **Mouse wheel over the Bell** = Q (0.05 per notch; Shift = 0.01 fine).
- **Ctrl + vertical drag** = Q with a log/perceptual mapping. The Q gesture starts lazily on the first Ctrl movement and ends when you release (or when you release Ctrl mid-drag, so it never gets stuck).

## Alt + click → individual Bell bypass

Holding **Alt** and clicking a Bell toggles its bypass — the same single command authority as the panel's Bypass button and the context menu.

## Delete / Backspace → delete

With a Bell selected, **Delete** or **Backspace** removes it (same command as the panel's DELETE button and the menu).

## Right-click → context menu

Right-clicking a Bell opens a menu anchored beside the Bell: **Bypass/Enable**, **Reset Gain**, **Reset Q**, **Reset Band**, **Delete**. Right-clicking empty space deselects and hides the inspector.

## Numeric editing

Double-click any value in the floating inspector (Frequency / Gain / Q) to open a text editor. **Enter** commits, **Escape** cancels, **Tab** cycles Frequency → Gain → Q so you can type a whole setup in sequence.

## Bypass vs. Delete — the essential difference

| | Bypass | Delete |
|---|---|---|
| Bell still exists? | **Yes** — it stays on the graph | **No** — it disappears |
| Frequency/Gain/Q retained? | **Yes**, all values persist | Gone |
| DSP contribution | Crossfades to identity (~5 ms) | Removed |
| Slot | Still occupied | **Becomes available again** |

Bypass is for A/B-ing a correction. Delete is for removing it. The DSP implements bypass as a fade to identity, never a "silent" Bell that keeps processing (Part XV).

---

# PART VI — INSPECTOR / MICRO HUD

When you select a Bell, a compact floating panel appears near the node (`BellControlPanel`, a child of the interactive surface — one surface-local geometry authority).

## Selected + idle → full compact inspector

Contains:
- **Bell identity** (B1/B2/B3),
- **Frequency**, **Gain**, **Q** — each draggable, wheel-adjustable, and double-click for numeric entry,
- **Bypass** — the compact power control,
- **Reset** — neutral secondary action,
- **Delete** — destructive action.

## Selected + dragging → micro HUD

While you drag the Bell (a *graph-origin* interaction), the full inspector collapses to a **tiny live-values readout** (104 px wide) — just the numbers, so the node and curve stay unobstructed while you work.

## Panel-origin manipulation → placement lock

When you interact *with the panel itself* (dragging a value field, wheel adjustment, numeric editing), the panel's position **locks** — it stays exactly under your hand while the values/node/curve change. Placement recalculates **once** after the gesture ends.

## Smart placement

The panel is placed intelligently near the node (`updatePosition`): it anchors near/above the node, **flips below only when the top would clip**, clamps near the edges, and always stays inside the analyzer, following the node while it is dragged. Placement is one of a small family (`above / below / left / right / clamped`) chosen by the placement engine. The panel does **not** run away from the cursor — mere cursor movement never moves it; only a completed gesture triggers one recalculation.

## Hover and deselection

- Hovering a Bell highlights it only — no inspector appears.
- Clicking empty graph space deselects and hides the inspector.

## Why this behavior exists

The interaction design exists so that **editing and reading never fight**. The full inspector appears only when you commit to a Bell; during a drag it shrinks to keep the graph visible (what you're actually watching); during panel edits the panel stays put so your eyes and hand agree; and placement only recalculates at gesture boundaries so the panel never appears to "run away." Every command — panel buttons, context menu, Alt-click, Delete key — routes through one command authority, so behavior is identical no matter how you invoke it.

---

# PART VII — ANALYZER

- **Purpose:** live visual feedback of what the track is doing, aligned with what G10's Mini EQ is doing to it.
- **Spectrum:** 140 log-spaced bars covering **20 Hz … 20 kHz**, computed from a 2048-point FFT of the *final* processed signal (post-EQ, post-character, post-Mini-EQ, post-bypass — exactly what the host receives).
- **Frequency grid:** log-spaced vertical lines (decades plus intermediate markers).
- **dB grid:** horizontal guides at 0, −6, −12, … −54 dB within the 0…−60 dB display range.
- **Response curve:** the Mini EQ's nominal response drawn over the spectrum — HPF + Bells + LPF, exactly as configured.
- **Nodes:** the HPF handle (left), the LPF handle (right), and the Bell nodes. Each Bell node *is* a handle: drag it to move frequency/gain, wheel for Q.

## The visual contract: one authority for everything

The most important analyzer property is a single coordinate contract:

```
VISIBLE RESPONSE CURVE PIXEL  ==  BELL NODE CENTER  ==  HIT-TEST CENTER  ==  PANEL ANCHOR
```

One plot-geometry authority (`ApexEqPlotGeometry`) defines the plot rectangle; the grid, the dB guides, the labels, the spectrum, the response curve, the node positions, the mouse hit-testing and the inspector anchoring all use the *same* bounds and the *same* frequency mapping. What you see is what you can click is where the panel points — they can never drift apart.

**Why this matters:** in earlier development, the curve, the nodes and the hit-testing used slightly different coordinate spaces, and nodes visibly floated above the response peaks. That class of bug is structurally impossible now because there is one coordinate source.

## High-Q sampling — why the curve always passes through the node

A response curve is drawn by sampling the analytical filter response at a set of frequencies. With *uniform* sampling, a very narrow high-Q peak (e.g., Q 40) can fall **between** two sample points — the rendered curve would visibly miss the peak the DSP actually produces.

G10 explicitly **inserts every Bell's center frequency into the curve's sample set**, so the rendered curve is guaranteed to pass through the analytical response at the critical frequency. For engineers: the sample set is `uniform points ∪ {bell center frequencies}`, evaluated with the exact z-domain transfer of the implemented biquads (the same coefficient clamping as the audio path), and a render-level regression test reads the actual painted curve vertices and asserts the node center coincides within 1 px.

---

# PART VIII — INPUT / OUTPUT LEVEL METERS

Two compact vertical peak meters (48×92 logical px — instrument-style bodies in the control strip, beside the knobs).

## Input meter — exact measurement point

Measured on the audio thread, once per host block: **the real signal entering G10's internal processing, after the Input trim** (the trim is a linear gain, so its dB adds to the measured peak). It is *not* the raw pre-trim signal — it is what the character stage and the musical core actually receive.

## Output meter — exact measurement point

Measured on the audio thread, once per host block: **the exact final buffer delivered to the host** — post-EQ, post-character, post-quality, post-trim, **post-bypass**. If G10 is bypassed, the Output meter shows the bypassed dry signal, because the meter measures the true final buffer (verified in `G10Processor::processBlock`).

## Channel authority

Both meters show the **louder peak of the available channels** for the compact single lane (`blockPeakDb` scans all channels and takes the maximum). A stereo pair never cancels; the display shows the true hot side.

## Why input/output metering matters

- **Gain staging:** see whether the character stage is being hit reasonably before you start EQing.
- **Identifying excessive boost:** cumulative band boosts show up on the Output meter long before you hear them as distortion.
- **Output compensation:** match Output against the dry level so A/B comparisons judge tone.
- **Level-matched bypass comparisons:** with Output matched, the global Bypass button becomes a true tone A/B.

## Display ballistics (explained simply)

The meters are *peak* meters with three phases:

- **Attack — fast/instant:** a new peak is shown immediately (it can never be missed).
- **Release — ~280 ms:** after the peak passes, the bar falls smoothly with a ~280 ms time constant.
- **Peak hold — ~600 ms:** a thin hold marker stays at the recent peak for ~600 ms so you can read the highest level after it passed.

Terminology: *attack* = how quickly the display rises to a new level; *release* = how quickly it falls back; *peak hold* = how long the maximum is remembered.

## Implementation (engineers)

```
Audio thread (per host block):
  peak = max |sample| over all channels of the measured buffer
  publish peak dBFS via a relaxed std::atomic<float>
  (no locks, no allocation, no UI calls, no FIFO)

Message thread (editor's 30 Hz timer):
  read the atomic
  apply display ballistics (instant attack, ~280 ms release, ~600 ms hold)
  repaint ONLY when the quantized bar/hold pixels actually moved
```

The audio thread never touches the UI, and the UI never touches the audio thread — the handoff is a single relaxed atomic float per meter.

---

# PART IX — GLOBAL BYPASS

G10 has **two distinct bypass concepts**; they must not be confused:

| | Global Bypass (BYPASS button) | Individual Bell bypass |
|---|---|---|
| Scope | The whole plugin | One Mini EQ Bell |
| State | `g10.bypass` parameter | `g10.bellN.bypass` parameters |
| Effect | The entire processing chain crosses to dry | Only that Bell's DSP contribution fades to identity |
| Curve/UI | UI still shows the curve (edits keep working) | Bell stays on the graph with F/G/Q intact |

## What actually happens in the DSP (from the frozen source)

The bypass lives **inside the processing engines**, not as a post-fade added by the processor:

- **Musical/character path** (`G10CurveEngineCore::processBlock` and `G10AnalogChainCore::processBlock`): a per-sample **equal-power crossfade** (`dry = sin(mix·π/2)`, `wet = cos(mix·π/2)`) over ~7 ms. Fully settled bypassed → the engine takes the **dry passthrough fast path** (no trim, no filter work, bit-identical to the input).
- **Mini EQ stage** (`G10MiniEqCore`): its own ~5 ms mix ramp (`bypassMix_`). Fully bypassed → `isSettledOff()` → the whole stage is skipped at block level (zero cost, bit-identical pass-through).
- **Settled vs. transition:** in the settled state both paths are *bit-exact* pass-through. During the transition (≈5–7 ms) the crossfades are per-sample and block-size independent, so entering/leaving bypass cannot click and produces no discontinuity.

## Does the Output meter reflect bypassed output?

**Yes.** The Output meter is measured on the *final buffer* after all processing including the bypass crossfades (`G10Processor::processBlock` stores `blockPeakDb` of the final buffer). When G10 is bypassed, the Output meter shows the dry level — because the meter measures the true signal the host receives.

## No duplicate bypass

The processor declares its existing `g10.bypass` parameter as the plugin bypass (`getBypassParameter()` returns `params_[kBypass]`). Without this, JUCE's VST3 client would synthesize a *second* host bypass control — a duplicate external parameter that changes nothing. There is exactly one bypass parameter, and it drives the DSP. `getTailLengthSeconds()` returns 0.0 (no declared tail; the IIR filters are minimum-phase with exponentially decaying — numerically finite — impulse responses).

---

# PART X — COMPLETE SIGNAL FLOW

Verified against the frozen source (`G10Processor::processBlock` / `processChunk`, `G10AnalogChainCore::processBlock`, `G10MiniEqCore::processBlock`). In v1.0 the canonical path is the analog character chain (always active — see Part XII); the clean engine exists as the frozen Phase-1 regression authority and as the analog crossfade source.

```
                 ┌────────────────────────────────────────────────┐
 HOST INPUT ────►│  Input trim  (smoothed ~15 ms, ±18 dB)         │  ◄── INPUT METER
                 └────────────────────────────────────────────────┘      (block peak
                        │                                                AFTER trim)
                        ▼
                 ┌────────────────────────────────────────────────┐
                 │  OVERSAMPLED CHARACTER PATH                    │
                 │  upsampler: zero-stuff + Butterworth AA (+×L)  │  NORMAL 2×  (realtime)
                 │  D1B  — discrete input saturation stage        │  HQ 4×     (offline)
                 │  TEN MUSICAL BANDS (frozen curve engine,       │
                 │                at the oversampled rate)        │
                 │  I1   — iron output stage (flux saturation)    │
                 │  downsampler: Butterworth AA + decimate        │
                 └────────────────────────────────────────────────┘
                        │
                        ▼
                 ┌────────────────────────────────────────────────┐
                 │  Output trim (smoothed ~15 ms, ±18 dB)         │
                 │  + bypass crossfade (~7 ms, inside the chain)  │
                 └────────────────────────────────────────────────┘
                        │
                        ▼
                 ┌────────────────────────────────────────────────┐
                 │  MINI CLEAN EQ (base rate, never oversampled)  │
                 │  HPF → B1 → B2 → B3 → LPF                      │
                 │  (stage bypass ~5 ms)                          │
                 └────────────────────────────────────────────────┘
                        │
            ┌───────────┴───────────┐
            ▼                       ▼
   Analyzer tap             HOST OUTPUT ────► OUTPUT METER
   (post-EQ, post-bypass)            (block peak of the EXACT final buffer)
```

## Exact ordering notes (engineers)

1. **Input trim** is applied at base rate, before oversampling (the character stages see the trimmed level).
2. **The ten musical bands run inside the oversampled domain** — same curve engine code as the frozen clean path, but at 2×/4× rate with unity trims and bypass disabled (the chain owns trims/bypass at base rate).
3. **The Mini EQ runs AFTER the complete musical/color path and after downsampling**, at the base rate. This is deliberate: the clean linear filters can never change how the signal excites D1B/I1, and the analyzer measures the true final output. The Mini EQ is *not* in the oversampled domain (linear filtering gains nothing from oversampling).
4. **Bypass/quality branches:** with global bypass engaged, both the chain's crossfade and the Mini EQ stage crossfade to dry (both converge on the original input). Quality (NORMAL vs HQ) selects which oversampled chain runs; during a quality transition both chains process the *same dry input in parallel* and crossfade over ~10 ms (HQ never receives NORMAL's processed output).
5. **Measurement points:** Input meter = after Input trim (block peak + trim dB). Output meter = the final host buffer (post-everything, post-bypass). Analyzer = same final signal (post-Mini EQ, post-bypass), pushed only while an editor is open.

## Realtime and offline paths

- **Realtime:** `isNonRealtime() == false` → quality target NORMAL → the 2× chain.
- **Offline (render/export):** `isNonRealtime() == true` → quality target HQ → the 4× chain.
- The quality decision is snapshotted in `prepareToPlay` (the host sets the non-realtime flag *before* prepare), so switching to offline never produces a mid-stream click; the 10 ms crossfade handles any residual transition.

---

# PART XI — THE TEN-BAND DSP AUTOPSY

All ten bands are built from shared primitives with per-family structure. Everything below is from `G10CurveEngineCore.h` (`G10BandEngine`).

## Shared primitives

- **TPT state-variable filter** (`G10SvfSection`) — Zavalishin topology-preserving transform: numerically well-conditioned at low frequencies, stable under coefficient interpolation, cheap. Three outputs (low/band/high) per section.
- **Bell mixing:** `x += (exp2(db·k) − 1) · band`, where `k = 0.16609640474436813` (the exact `log2(10)/20`). The band output is normalized by `1/Q` (`band = v1·k_`), so **the bell contribution reads exactly the fader dB at the anchor frequency** (the raw TPT band peaks at Q).
- **RBJ high-shelf biquad** (`G10ShelfBiquad`, TDF2) — used by AIR16K because the SVF highpass-mix is *not* an exact shelf (measured up to ~1.0 dB error at 8 kHz and a spurious ~1.2 dB scoop at 4 kHz).
- **Smoothing:** one-pole per-sample, ~20 ms time constant for bands (shared across channels; advances exactly once per sample, so mono/stereo trajectories are identical and block size does not matter).
- **Coefficient policy:** lazy bounded update — coefficients are recomputed only when the smoothed gain moved more than 1e-4 dB; otherwise the filter coefficients stay put (no wasted trig).
- **Gain law:** each family splits the fader dB across sections with weights (`gainWeightA_/B_`, `supportWeight_`); the `(A−1)` form means **all-zero faders = bit-exact pass-through** (neutral contract).
- **Minimum-phase:** every band is pure IIR (SVF/shelf biquad), zero latency.
- **Denormal flush** (1e-30) on every filter state and the final sample.

## Per-family structure and Q laws

Q laws use the proportional-Q principle: `n = |dB|/12`, small movements broad, larger movements focused. Boost/cut asymmetry is embedded per family.

| Band | Structure | Q law (Q = …) |
|---|---|---|
| DEEP 31 Hz | Compound: bell (55% of dB) + very broad low shelf (45%) anchored at 45 Hz (Q 0.55) | `0.30 + 0.18·n^1.30` |
| PUNCH 63 Hz | Proportional-Q bell + subtle support contour at 120 Hz (5% of dB, Q 0.80) | `0.50 + 0.40·n^1.05` |
| BODY 125 Hz | Hybrid bell/shelf: boost 70/30, cut 83/17; shelf anchor 110 Hz (Q 0.60) | `0.42 + 0.30·n^1.00` |
| WARMTH 250 Hz | Bell; boost broader than cut | boost `0.45 + 0.43·n^1.10`; cut `0.52 + 0.53·n^1.10` |
| WOOD 500 Hz | Bell; boost more focused than cut | boost `0.55 + 0.50·n^1.05`; cut `0.45 + 0.45·n^1.05` |
| FOCUS 1 kHz | Pure bell | `0.65 + 0.60·n^1.00` |
| ATTACK 2 kHz | Pure bell | `0.65 + 0.70·n^1.05` |
| PRESENCE 4 kHz | Pure bell — the most focused band; boost tighter than cut | boost `0.75 + 0.70·n^1.00`; cut `0.55 + 0.55·n^1.00` |
| SHINE 8 kHz | Fixed broad bell (Q 0.80) + continuous bell↔shelf **morph** | fixed Q 0.80; morph: boost `(dB−2)/10` (≈40% shelf at +6 dB), cut `0.25·|dB|/12` capped |
| AIR 16 kHz | Exact RBJ high shelf (TDF2) + subtle support contour at 9 kHz (boost 10%, cut 5% of dB, Q 0.80) | `0.50·sqrt(10^(dB/20))` (gain-dependent shelf Q); turnover `min(8000, 0.72·fs/2)` |

## Interaction between neighboring bands

Bands process the sample **in series** (31 → 16 kHz), each contributing its shaped component additively. There is no cross-band coupling beyond the cascade; the "neighbor" behavior you hear comes from the deliberate overlap of the broad low-frequency curves (DEEP/PUNCH/BODY) and the mid stack (WARMTH…PRESENCE), which is designed so a small boost on one band still reads as a musical movement rather than an isolated filter.

## Why the response has its final shape

Every structural parameter (weights, shelf anchors, support contours, morph laws, Q laws) was measured and fixed during development as the accepted musical contract — the source records them as constants with the measurements that justified them (e.g., the AIR shelf's net gain at the 16 kHz anchor ≈ 6.0 dB at +6 within ±0.1 dB across 44.1–192 kHz). The internal lineage comments (`G10Types.h`) name classic units as **design inspiration** — e.g., DEEP ← Pultec EQP-1A philosophy, PUNCH ← SSL/API philosophy, BODY ← Neve 1073 philosophy, AIR ← Maag Air Band philosophy. **G10 is not an emulation of any of these units**; it is an original design informed by their philosophies.

---

# PART XII — D1B AUTOPSY

D1B is the **discrete input stage** — the first saturation in the chain (`G10DiscreteInputStageCore`), running at the oversampled rate, right after the upsampler and before the musical bands.

## Plain English

D1B is the "transistor-style" warm-up stage. It adds a small, controlled amount of the classic odd-harmonic saturation plus a *much smaller* even-harmonic touch, and it removes the DC that the even part would otherwise create. At normal levels it is subtle; it grows as the signal gets hotter; at −18 dBFS it is effectively transparent.

## Engineering terms

The exact accepted topology:

```
residual = (tanh(x) − x) · kSaturation + kAsymmetry · x · tanh(x)
dcState  += (residual − dcState) · dcCoeff        (one-pole LP, 2 Hz cutoff)
y        =  x + residual − dcState
```

with `kSaturation = 0.5`, `kAsymmetry = 0.05`, `kDcBlockerHz = 2.0`.

- **Odd core:** `(tanh(x) − x)` is the saturation-excess form — pure odd harmonics (H3, H5, …), the "warm" component.
- **Even residual:** `x·tanh(x)` adds a controlled musical asymmetry — a small even-harmonic (H2) component, giving the stage its slight character.
- **DC handling:** the 2 Hz one-pole block operates on the *nonlinear residual only*. The linear path is exactly `x`, so there is **no low-cut on the audio signal** (fundamental gain 0.000 dB at every frequency) while the DC created by the even residual is removed (≥26 dB attenuation at 40 Hz, ≥60 dB at 1 kHz). The cutoff stays 2 Hz at any sample rate (`dcCoeff` is sample-rate derived).
- **Level dependence:** documented operating points — at 0 dBFS the stage contributes ≈ **−1.1 dB** saturation loss; at −18 dBFS it is effectively transparent. The saturation is real (measurable THD), not a loudness trick.
- **Harmonics:** only *deterministic nonlinear components* — `tanh` is a smooth deterministic function; the stage creates no noise. Per-channel independent state.
- **Oversampling relationship:** nonlinear stages generate harmonics above the original Nyquist; the oversampled domain + anti-alias filters keep those harmonics from folding back as aliases. This is why the saturation runs *after* the upsampler (see Part XIV).
- **Stereo behavior:** identical per-channel processing with independent DC state; no cross-channel coupling.

---

# PART XIII — I1 AUTOPSY

I1 is the **iron output stage** (`G10IronOutputStageCore`) — output-transformer-inspired saturation, running at the oversampled rate after the musical bands, before the downsampler.

## Plain English

I1 makes the saturation *stronger when there's low-frequency energy*. It watches a slow "flux" estimate of the signal (a leaky average of what has passed, weighted toward low frequencies) and uses it to modulate how hard the stage saturates. The result: bass pushes the stage into a slightly richer, "iron-like" behavior, while quiet or high-frequency material stays clean. It never adds bass of its own — only the amount of saturation changes.

## Engineering terms

```
fluxRaw  += (x − fluxRaw) · fluxCoeff      (one-pole LP, kFluxHz = 60 Hz)
flux     = tanh(fluxRaw)                    (bounded, |flux| ≤ 1)
strength = kSaturation · (1 + kFluxDepth · |flux|)
y        = x + (tanh(x) − x) · strength
```

with `kSaturation = 0.5`, `kFluxHz = 60.0`, `kFluxDepth = 0.9`.

- **Flux behavior:** the 60 Hz leaky integrator accumulates more per cycle at lower frequencies, so at equal amplitude the flux excursion — and therefore the saturation strength — grows toward the bottom of the spectrum. That is the required "iron" behavior: strongest saturation at low frequencies.
- **Modulation:** flux modulates the *nonlinear strength only*; the linear term is `x`, so no bass is added and the low-level response stays flat.
- **Monotonicity:** `kFluxDepth = 0.9` caps strength at `0.5 · 1.9 = 0.95`, keeping the static transfer monotonic for every input — no foldback even at +18 dB trim.
- **No DC:** the flux state is a lowpass of a zero-mean signal → zero-mean; the stage produces no DC.
- **What makes it different from D1B:** D1B is *static* saturation (odd core + small even touch, with DC removal) — its amount depends only on the instantaneous level. I1 is *modulated* saturation (odd-only) — its amount depends on the recent low-frequency energy. Together they form the "transistor input → iron output" pairing: D1B colors, I1 responds to the music's low-end energy.
- **Stereo:** per-channel state, independent, deterministic, denormal-safe.

---

# PART XIV — OVERSAMPLING / QUALITY

## Final policy (frozen)

| Mode | When | Chain |
|---|---|---|
| **NORMAL** | Realtime | 2× oversampled character chain |
| **HQ** | Offline / non-realtime (render, export) | 4× oversampled character chain |

The processor reads `isNonRealtime()` before `prepareToPlay` (the host sets the flag before prepare), so the quality is correct from the first block and the transition is click-free by construction; any residual change crossfades over ~10 ms with **both chains processing the same dry input in parallel** (HQ never receives NORMAL's processed output).

## What is oversampled and what is not

- **Oversampled:** D1B, the ten musical bands, I1 — everything with nonlinearity or musical curves. The curve engine runs *at the oversampled rate*.
- **NOT oversampled:** the Mini Clean EQ (pure linear filtering — oversampling buys nothing), the trims, the bypass crossfades, the meters, the analyzer. The Mini EQ deliberately runs at base rate *after* the oversampled domain.

## Anti-alias design (measured policy, frozen)

- **2×:** 8th-order Butterworth AA, cutoff at **1.1× the base Nyquist** (26.4 kHz at 48 kHz base).
- **4×:** 10th-order Butterworth AA, cutoff at **1.0208333× base Nyquist** (24.5 kHz at 48 kHz base). The bilinear (tan) warp stretches the 4× stopband over 24–96 kHz; the same absolute cutoff as 2× would give ~6.5 dB *weaker* rejection at 30 kHz, so the 10th-order/lower-cutoff design restores the product ordering — 4× beats 2× at the 10 kHz stress case (−59.5 vs −57.5 dB) and at 20 kHz (−45.0 vs −40.8 dB) while keeping the 20 kHz passband within −0.05 dB.
- Upsampler: zero-stuff + AA + **×L interpolation normalization** (zero insertion alone scales by 1/L; the ×L restores the original amplitude so the nonlinear stages see the same level regardless of factor). Downsampler: AA + decimate.

## Latency and CPU

- **Latency: zero in every mode.** All AA filters are minimum-phase IIR (Butterworth TDF2 sections) — no FIR delay, no lookahead, no delay lines. `setLatencySamples(0)` is published in `prepareToPlay` and can never go stale.
- **CPU:** the 4× path does roughly twice the per-sample oversampled work of the 2× path; beyond that, no CPU figures are claimed here because none were measured as a product contract. Realtime operation uses NORMAL by design; offline renders can afford HQ.

## Legacy Analog/Quality parameter compatibility

- `g10.analog` and `g10.quality` are **serialized for compatibility** but are **inert in the audio path**: the analog/color path is *always active* (the canonical identity), and quality is decided by the host's realtime/offline flag, not by the parameter.
- Externally (public VST3), both parameters are **hidden**: they exist as state-only objects (`stateOnlyParameters_`) so old projects restore intact, but no external control is exposed for them. In the APEX-native exposure they are hosted for compatibility with old chain documents.
- This means a v1-era project that stored "Analog off" still *loads* correctly, and its sound remains exactly the current G10 identity.

---

# PART XV — MINI EQ DSP AUTOPSY

Everything below is `G10MiniEqCore.h`.

## HPF / LPF

- **Topology:** 2nd-order Butterworth (Q = 0.70710678), 12 dB/oct, realized as a **TPT state-variable filter** (`G10SvfSection` — the same primitive the musical core uses). The HPF uses the SVF high output; the LPF the low output.
- **Coefficient policy:** coefficients are recomputed **per sample** from the smoothed cutoff (the SVF is cheap and stable under coefficient interpolation; the smoothed parameter moves slowly, so this is bounded work).
- **State:** per-channel SVF integrator state (`ic1eq_/ic2eq_`), denormal-flushed.
- **Smoothing:** the *cutoff parameter* and the *stage mix* are smoothed per-sample with a ~5 ms one-pole; a stage that is fading out keeps its parameters frozen at the last active tuning (constant response during the fade = click-free), and parameters snap to the OFF endpoint only after the mix has fully left.

## Bell filters (B1/B2/B3)

- **Topology:** clean **RBJ peaking biquad** (direct form 2 transposed), the same family as the frozen musical shelf biquad but for peaking. Gain 0 dB reduces to identity coefficients (b == a), so an enabled 0 dB bell is skipped by the caller before processing.
- **Mapping (v3 laws):**
  - frequency: `20 · 1000^p` Hz (p = normalized; 20 Hz…20 kHz, log)
  - gain: `−12 + 24p` dB (−12…+12, linear)
  - Q: `0.10 · 400^p` (0.10…40.0, log)
- **Coefficient math:** `A = 10^(db/40)`, `w0 = 2π·f0/fs` (f0 clamped below Nyquist), `alpha = sin(w0)/(2·max(0.05, Q))`, then the standard RBJ peaking coefficients normalized by a0. TDF2 keeps the state bounded.
- **Nyquist safety:** coefficient generation clamps the effective cutoff (bell f0 clamped to `fs/2 − 1`; the documented internal safety fraction is 0.45·fs). **The stored/public parameter is never altered by the sample rate** — clamping happens only at coefficient time.
- **Parameter smoothing:** per-sample one-pole toward the targets; frozen while the bell is fading out; **snapped instantly when re-activating from silence** (see below).

## The ~5 ms bypass identity transition

A Bell that is bypassed (or disabled) does not "stay quiet while still processing." Its mix ramps to exactly 0 over ~5 ms (`kSmoothSeconds = 0.005`), and the per-bell smoothing freezes. Re-activation adopts the **current** F/G/Q instantly while the mix is still exactly 0 — inaudible by construction — and only the ~5 ms mix fade-in is audible. Un-bypass therefore starts from the newly edited configuration.

## Silent-state optimization (engineers)

Two related optimizations:

1. **Block-level settled-skip:** when every stage is settled OFF (all mixes exactly 0, or the whole stage bypassed), `processBlock` returns immediately (`isSettledOff()` + `targetsAllOff` → `resetFilterStates()` for clean re-entry). The samples are **bit-identical to the input** — zero per-sample DSP cost.
2. **Frozen smoothers + hidden snap:** while a Bell is fully out of circuit and the rest of the stage is off, *no smoothing runs at all* (the early-skip guarantees it). When re-activation begins, the hidden targets are snapped in one step while the contribution is still exactly 0, then only the audible mix fades in.

Why both matter: a naive implementation would either keep running hidden ramps (wasted DSP — a fully off Mini EQ should cost nothing) or would smooth the parameters from stale positions (audible parameter sweeps on re-enable). The snap-while-zero design gives you instant, inaudible adoption of edits made while bypassed, and the settled-skip gives bit-identical pass-through at zero cost when the layer is off.

---

# PART XVI — PARAMETER ARCHITECTURE

**Final contract (verified in `G10Types.h` / `G10Processor.h`):** **32 native internal parameters**, of which **30 are exposed to the external VST3** (`kNumPublicVst3Parameters = kNumParams − 2`). The two excluded are `g10.analog` and `g10.quality` (legacy compatibility, hidden externally as intended).

| Index | ID | Display name | Range | Default | Kind | External VST3 | Serialized | Audible |
|---|---|---|---|---|---|---|---|---|
| 0 | `g10.input` | Input | −18…+18 dB | 0 dB | Trim | yes | yes | yes |
| 1–10 | `g10.band31` … `g10.band16k` | DEEP…AIR | −12…+12 dB | 0 dB | Band | yes | yes | yes |
| 11 | `g10.output` | Output | −18…+18 dB | 0 dB | Trim | yes | yes | yes |
| 12 | `g10.analog` | Analog | 0/1 | 0 | Toggle | **no** | yes | **no** (inert) |
| 13 | `g10.quality` | Quality | 0/1 | 0 | Toggle | **no** | yes | **no** (inert) |
| 14 | `g10.bypass` | Bypass | 0/1 | 0 | Toggle | yes | yes | yes (dry) |
| 15 | `g10.hpf` | HPF | 20 Hz…20 kHz / OFF | OFF | HzHpf | yes | yes | yes |
| 16 | `g10.lpf` | LPF | 20 Hz…20 kHz / OFF | OFF | HzLpf | yes | yes | yes |
| 17–20 | `g10.bell1.enabled/freq/gain/q` | B1 Enabled/Freq/Gain/Q | 0/1; 20 Hz…20 kHz; −12…+12 dB; Q 0.10…40 | off / 1 kHz / 0 dB / Q 1 | Toggle/HzBell/BellGain/BellQ | yes | yes | yes |
| 21–24 | `g10.bell2.*` | B2 … | same | same | same | yes | yes | yes |
| 25–28 | `g10.bell3.*` | B3 … | same | same | same | yes | yes | yes |
| 29 | `g10.bell1.bypass` | B1 Bypass | 0/1 | 0 (not bypassed) | Toggle | yes | yes | yes (fade) |
| 30 | `g10.bell2.bypass` | B2 Bypass | 0/1 | 0 | Toggle | yes | yes | yes (fade) |
| 31 | `g10.bell3.bypass` | B3 Bypass | 0/1 | 0 | Toggle | yes | yes | yes (fade) |

Notes:

- **Stable IDs and ordering:** the semantic index order is immutable (IDs are the persistence ABI). Mini EQ parameters were appended after the frozen 15; v3 Bell bypasses were appended after the v2 IDs so no earlier index ever moved. The processor keeps a hosted-index → semantic-index map (`hostedParameterSemanticIndexes_`) because JUCE compacts hosted indices.
- **Automation:** in the APEX-native exposure all 32 are hosted and bound to the automation system by stable string ID; externally, 30 are exposed. All controls report text like "+6.0 dB", "3.17 kHz", "Q 5.20" via the parameter `getText` mapping.
- **Duplicate-bypass prevention:** `getBypassParameter()` returns the existing `g10.bypass` (index 14), so the VST3 client does not synthesize a second bypass (Part IX).
- **Analog/Quality hiding:** in the public VST3 exposure the two legacy parameters are constructed as **state-only objects** — owned, serialized, restorable, but never hosted, so no external control exists for them and no external automation can write them. The DSP never reads them (identity/quality are decided elsewhere).

---

# PART XVII — STATE FORMAT / MIGRATION

State is a JUCE **ValueTree** (`g10state` tag) with a `version` property. Current schema: **v3**.

| Version | What it contains |
|---|---|
| **v1** | The frozen 15-parameter musical G10 (Input, 10 bands, Output, Analog, Quality, Bypass). No Mini EQ fields. |
| **v2** | v1 + the 14 Mini Clean EQ parameters, under the *old* control laws (HPF 20…420 Hz, LPF 8.5…16 kHz, Bell Q 0.5…10). |
| **v3** (current) | v2 + the 3 individual Bell bypass parameters, and re-mapped control laws: HPF/LPF 20 Hz…20 kHz (log), Bell Q 0.10…40 (log). |

## Restore behavior (tolerant, verified in `setStateInformation`)

- Unknown fields are ignored.
- **Missing fields keep defaults** — so a v1 state restores with Mini EQ fully off (HPF OFF, LPF OFF, all Bells disabled, no bypasses), exactly like old G10.
- Out-of-range values are clamped by `G10Parameter::setValue`; **non-finite values are skipped** (defaults kept).
- Every field is additionally guarded by `hasProperty` (defense in depth: a v2 state may legitimately omit Mini EQ fields).

## Migration philosophy

When a control **law** changes (v2 → v3), a stored *normalized* value no longer means the same physical thing. The migration therefore never reuses the raw number:

```
old normalized value  →  decode through the EXACT old law  →  old physical Hz/Q
old physical Hz/Q     →  encode through the NEW law        →  new normalized value
```

This preserves the **old session sound**: the physical cutoff/Q the user heard is restored, not a different frequency wearing the same normalized number.

**Why copying the normalized value directly is wrong:** normalized 0.5 meant "≈ 63 Hz HPF" under the old law and "≈ 632 Hz" under the new one. Reusing the number would silently move every stored filter. The decode→encode path is the only correct migration.

## Exact legacy (v2) laws, preserved for migration

```
HPF    : 20 · 21^p Hz                       (20…420 Hz; p == 0 → OFF)
LPF    : 8500 · (16500/8500)^p Hz           (8.5…16 kHz nominal; law headroom 16500
                                             so 16 kHz was an ACTIVE position at p < 1)
Bell Q : 0.5 · 20^p                         (0.5…10)
```

Bell frequency and gain laws did not change in v3, so only **HPF, LPF and Bell Q** are migrated. The migration runs **strictly for `version == 2`**: a v1-shaped state with stray Mini-EQ-looking properties is a raw restore, never a v2 semantic value (a v1 state must stay at defaults).

## How bypass state is preserved

All three Bell bypasses are ordinary serialized parameters (`g10.bellN.bypass`) restored through the same tolerant path; v2/v1 states lack them → they restore to the default (not bypassed). Bypass is never "derived" or reconstructed — it is stored state, exactly like every other parameter, and the DSP fades it in/out per the ~5 ms identity transition (Part XV).

---

# PART XVIII — REALTIME SAFETY

This is an engineering chapter. The frozen audio-thread contract (`G10Processor`, `G10CurveEngineCore`, `G10AnalogEngineCore`, `G10MiniEqCore`):

- **Zero heap allocation on the audio thread.** Every buffer and filter state is a fixed-size member, preallocated in `prepareToPlay` for the maximum oversampling factor (4×) and the prepared maximum block size. `prepare()`/`reset()` run on the message thread before audio starts.
- **Zero locks.** The only cross-thread state is:
  - parameter values — plain floats, written by the host control thread, read by the audio thread (the canonical JUCE pattern),
  - the meter handoff — two `std::atomic<float>` (relaxed), written once per block, read by the UI timer,
  - the analyzer gate — one `std::atomic<bool>` (`analyzerActive_`).
- **Scratch buffers:** `scratchA_`/`scratchB_` (processor), `dryScratch_`/`osBuffer_` (analog chain) are sized once in prepare and only ever read/written on the audio thread.
- **Chunking (oversized host blocks):** JUCE says "program defensively in case a buggy host exceeds this value". If a host submits a block larger than the prepared maximum, the processor processes the complete buffer in consecutive bounded chunks (`chunkCapacity = scratchA_.getNumSamples()`). All DSP state is member state preserved across chunk boundaries, so chunking is **bit-equivalent** to receiving the same audio as several legal smaller blocks — no allocation, no locks, no resize.
- **Parameter smoothing:** all smoothing (bands ~20 ms, trims ~15 ms, bypass ~7 ms, Mini EQ ~5 ms) advances exactly once per sample inside the loops — block-size independent, click-free, shared across channels so L/R see identical trajectories.
- **GUI separation:** the editor's single ~30 Hz timer drives analyzer measurement, Mini EQ surface refresh, meter ballistics and blink decisions. The audio thread only pushes samples into the lock-free SPSC analyzer FIFO (while an editor is open) and stores meter peaks; it never touches components, strings or the GUI.
- **Denormal safety:** every filter state and final sample is flushed below 1e-30.

## Historical lessons vs. current architecture (explicitly labeled)

- **Oversized host block (HISTORICAL bug, fixed):** a development-era HostSmoke harness declared a 512-sample maximum block and later submitted 1024/4096 samples through the JUCE VST3 host wrapper. The heap corruption was detected only during destruction — a classic "ghost crash" — and ASan proved the harness overflow, not a processor fault. **Current architecture:** the declared maximum is enforced consistently everywhere *and* the processor defensively chunks any oversized host buffer, so even a misbehaving host cannot overflow the preallocated state.
- **Sample-rate lifecycle (HISTORICAL bug, fixed):** `getSampleRate()` returned 0 because `setRateAndBufferSizeDetails` was never called, making the response surface evaluate cascade math at 1 Hz. **Current architecture:** `prepareToPlay` stores the real rate, and every consumer reads the processor's prepared rate.
- The current points above (allocations/locks/atomics/scratch/chunking/smoothing) describe the **frozen v1.0 architecture**, not historical behavior.

---

# PART XIX — HOST / RESIZE ARCHITECTURE

Engineering reference (users can skip this part).

## Three sizes, kept separate

1. **Host window size** — the floating plugin window the host creates; user-resizable.
2. **Plugin editor logical size** — G10's design space: **1000×612** logical px (0.625 × the approved 1600×980 blueprint).
3. **Host scale factor** — JUCE's editor-level `setScaleFactor`/hostScaleTransform, owned by the host/format layer, distinct from anything G10 does internally.

## The resize policy (`PluginEditorResizePolicy`)

When a plugin editor is attached, the host classifies it once (message thread):

- **trueResize** — the editor is natively resizable; the host gives it the full content area and the editor's own constrainer negotiates the final size. **G10 is classified trueResize** (its editor is resizable within 500×306 … 2000×1224).
- **hostScale** — fixed-size editor; the JUCE-sanctioned host zoom path (`setScaleFactor`, or the VST3 format-layer content-scale negotiation) applies, with safe snap-back when the plugin cannot scale. Zoom bounds: 0.25×…4×.
- **fallback** — scaling known-unsafe: center at native size, clamp the window so it can never clip the editor. The fallback, not the default.

The floating window itself stays resizable in every mode; only the *editor placement* follows the policy.

## G10's editor resize mechanics

The outer editor accepts **any host-negotiated aspect ratio** (a real host like REAPER fills whatever area the user picks — the plugin area must never be forced to 1000:612). The design aspect is preserved on an internal **Content child** that carries a **uniform min-fit scale transform** (`G10Editor::resized`): logical bounds stay 1000×612, only the transform scales, and the child is centered with the letterbox area painted by the editor's own shell gradient — so leftover space belongs to G10, never to the host.

## The historical double-scaling (c²) bug — why it cannot return

**HISTORICAL:** an earlier approach scaled the Content's *bounds* AND applied a scale transform — bounds×transform squared — overflowing the visible viewport in DPI-scaled hosts. **Current architecture:** the Content keeps its **logical bounds** and only the transform scales; JUCE's `getLocalArea`/hit-testing correctly invert the transform, so rendering, host size negotiation and mouse coordinates all agree. Rendering and hit-testing scale together by construction.

## REAPER "(mono)" preferred-bus issue (HISTORICAL, resolved in v1.0)

**HISTORICAL:** the preferred channel configurations were declared mono-first (`{1,1}, {2,2}`), and a real REAPER host session defaulted the instance to **"(mono)"**, collapsing a stereo track to mono on insert. **Final state:** the product declares **stereo-first** ordering (`{2,2}, {1,1}`) in both the project file (`G10VST3.jucer`) and the generated defines (`JucePluginDefines.h`), and HostSmoke validates the exposed bus metadata against real host expectations. `isBusesLayoutSupported` accepts mono and stereo with input == output.

---

# PART XX — PERFORMANCE CHARACTERISTICS

Only claims supported by the frozen source and its tests are listed. **No CPU percentages or instance counts are stated** — none were measured as a product contract.

- **Latency:** 0 samples in every mode (all-IIR, minimum-phase; `setLatencySamples(0)` published).
- **Oversampling:** realtime NORMAL 2×; offline HQ 4×; quality crossfade ~10 ms with parallel chains; anti-alias per Part XIV.
- **Neutral contract:** all bands/trims at 0 dB, bypass off → output **bit-exact** equal to input (every (A−1) multiplier exactly 0, every trim gain exactly 1).
- **Mini EQ settled-skip:** fully-off Mini EQ costs nothing per block and passes samples bit-identically.
- **Analyzer cadence:** measurement at the editor's **~30 Hz** timer (2048-point stereo FFT); visual presentation interpolates at the display rate (60/120/144 Hz). The audio thread pushes only while an editor is open (gate `analyzerActive_`), so the analyzer costs nothing with the UI closed.
- **Meter cadence:** audio thread stores one peak per block (relaxed atomic); the 30 Hz UI timer applies ballistics and repaints only on quantized change.
- **Block-size independence:** all smoothing/crossfade state advances per sample; output is independent of block size; oversized host blocks are chunked bit-equivalently.

---

# PART XXI — WHAT G10 IS NOT

Product boundaries are part of the design. G10 v1.0 is **not**:

- an unlimited parametric EQ (three surgical Bells, deliberately),
- a dynamic EQ,
- a multiband compressor,
- a spectral repair / de-noise system,
- a full M/S mastering EQ,
- a linear-phase surgical mastering EQ,
- a hardware-perfect clone of any classic unit (e.g., an API 560 circuit copy).

Its identity is deliberately:

```
10 musical bands + HPF + 3 surgical Bells + LPF + character (D1B + I1)
+ analyzer + input/output gain staging
```

Product completeness does not require feature overload. Every element of G10 exists to serve the "musical shaping + surgical correction" workflow; adding more (more bells, M/S, dynamics) would blur the two-layer identity this instrument is built around. If a task needs a mastering-grade linear-phase EQ or a dynamic EQ, that is a different tool for a different job — and G10 makes no claim to replace it.

---

# PART XXII — REAL-WORLD RECIPES

Usage examples, not guarantees — **mix by ear**. Levels, sources and rooms differ; these are starting points that illustrate how the two layers work together.

1. **Lead vocal.** Start with 2 kHz (ATTACK) around +1…+2 dB for articulation; 8 kHz (SHINE) +1…+2 for air; cut 250–500 Hz a little if boxy. Use a Bell on the analyzer to tame a specific harsh peak (find it visually, double-click, narrow the Q to ~8–12, cut 2–4 dB). HPF 80–120 Hz to clean rumble and low-end thump.
2. **Rap vocal.** Cut 250–400 Hz mud (WARMTH down 2–4 dB); PRESENCE 4 kHz up for intelligibility; a narrow Bell at the sibilance band (~7–9 kHz) if needed for de-essing support; LPF 14–16 kHz to reduce hiss when the verse is dense.
3. **Vocal bus.** Gentle 125 Hz (BODY) +1 and 8 kHz +1 across the group; a Bell at the group's common resonance (often 300–400 Hz) cut 1–2 dB at Q ~6–8. Use the Output knob to match the bus level after summing boosts.
4. **Drum bus.** 63 Hz (PUNCH) +2…+4 for slam; cut 300–500 Hz for boxiness; 4 kHz (PRESENCE) +1…+2 for attack; air 16 kHz +1 for cymbal sparkle. Check the Output meter — drum buses accumulate level fast.
5. **Bass.** 31 Hz (DEEP) +2…+4 for sub weight; cut 200–300 Hz if it conflicts with guitars; a Bell around 80–120 Hz to remove a resonant room note (Q ~4–6, cut 2–3 dB). HPF below 30–40 Hz if the source has useless sub-30 Hz energy.
6. **Acoustic instrument.** 500 Hz (WOOD) +1…+2 for body/woodiness; cut 1–2 kHz if it pokes; 8 kHz +1…+2 for string detail; HPF 60–100 Hz to remove stand/room rumble.
7. **Mix bus.** Broad musical moves only: 125 Hz +0.5…+1, 2 kHz +0.5…+1, 8 kHz +0.5…+1; surgical Bells only for a real problem (e.g., a 200 Hz room bump). The ten faders are the right tool here; the Mini EQ is for when a specific frequency misbehaves.
8. **Resonance removal.** Watch the analyzer while the track plays; find the persistent narrow bump; double-click exactly on it; set Q ~10–40 (wheel or Ctrl-drag); cut until the bump flattens. Alt-click to A/B the correction instantly.
9. **Brightening without harshness.** Boost 8 kHz (SHINE) rather than 16 kHz — Shine morphs toward a shelf, so it adds top without a ringing bell; then add a touch of 16 kHz AIR only if the track takes it. If 4–6 kHz still bites, cut PRESENCE a hair rather than dulling the top.
10. **Gain-matched A/B.** Set the tone you want; then set Output so the Output meter matches the Input meter (or match by ear against the dry signal); engage BYPASS and listen — the difference is tone, not level.

---

# PART XXIII — TROUBLESHOOTING / FAQ

**Why can't I create a fourth Bell?** The Mini EQ is deliberately limited to three Bells (Part IV). The limit is a product contract, enforced everywhere; double-clicking when full shows "3/3 Bells active". Bypassing a Bell does *not* free its slot — only Delete does.

**Why did my Bell disappear?** You (or a previous edit) deleted it — Delete/Backspace, the panel's DELETE button, or the context menu all run the same command. Deletion is not undoable in v1.0; the slot becomes available again.

**Difference between bypassing and deleting?** Bypass keeps the Bell (F/G/Q intact) and fades its DSP contribution to identity over ~5 ms; delete removes it and frees the slot. Bypass is for A/B; delete is for removal (Part V).

**Why does Q 40 look extremely narrow?** Because it *is* extremely narrow — that's the point of the range's top end. The rendered curve is guaranteed to pass exactly through the Bell's center (Part VII), so the visible spike is the true response.

**Why does the panel move after I finish editing?** Placement is static *during* a panel interaction (the placement lock keeps it under your hand) and recalculates once when the gesture ends — the smart non-obstructive placement (Part VI) may then reposition it.

**Why does it NOT move while I'm editing it?** Same contract: panel-origin gestures freeze the panel position; only a completed gesture triggers one recalculation.

**Why does the Output meter differ from the Input meter?** They measure different points: Input is after the Input trim (before processing); Output is the exact final host buffer — after boosts, cuts, character saturation and bypass. The difference is, in a literal sense, what G10 is doing.

**Why can the filter still have a tail after input goes silent?** G10 declares no tail (`getTailLengthSeconds() == 0.0`) and all filters are minimum-phase IIR. An IIR impulse response decays exponentially rather than ending abruptly — a brief, numerically finite settling after a loud passage is the filter's stored energy ringing down, not a bug and not a declared tail.

**Why don't I see Analog or Quality controls?** Both are legacy compatibility parameters: serialized for old projects, inert in the audio path, and deliberately hidden from the external VST3 surface (Part XIV, Part XVI). The character (D1B + I1) is always active by design; quality follows the host's realtime/offline state.

**Why is G10 minimum-phase?** Every stage is an IIR filter with no lookahead (no FIR delay lines), so the total latency is 0 samples. Minimum-phase also means the EQ's phase shift is the minimum possible for its magnitude response.

**Why only three surgical Bells?** Product identity: a small, deliberate surgical layer next to the musical core (Part IV, Part XXI). Three covers resonance/harshness/carve work without turning G10 into an unbounded parametric.

**Why does double-clicking a fader reset it?** It is a protected interaction contract: double-click delivers an exact 0 dB reset via a proper host gesture (begin → set → end), and the second click is never mistaken for the start of a drag — so the reset can never leave a stuck gesture (Part III).

---

# PART XXIV — VALIDATED ENGINEERING EVIDENCE

Final frozen validation (from the release record `docs/releases/APEX_G10_WINDOWS_V1_0_FINAL.md`):

| Item | Result |
|---|---|
| Source freeze | commit `df319fd5410286b0318eddac3e07281d708921e6`, tag `g10-windows-v1.0-final` |
| G10.Processor | 19 groups / 458 assertions passed / 0 failed (seed 0xA9E12026) |
| G10.EditorHook | 13 groups / 505 assertions passed / 0 failed (seed 0xA9E12026) |
| MSVC AddressSanitizer (EditorHook) | 0 violations / EXIT 0 |
| HostSmoke (golden tagged build) | 331 PASS / 0 FAIL / EXIT 0 |
| GOLDEN == PUBLIC VST3 SHA-256 | `4E10213E55132F3E640B0FE9A95830058B7E5A8B855818262AEB5A91FC795FD2` |
| APEX Release (DAW host) | `DAW_Core.exe` SHA-256 `6100E87511B32DECD421C06B76C41C8FD3A08E89A5C15443C9D5A9DE3BB94A8B` |

**What these tests PROVE:** engineering evidence for defined contracts — parameter counts and ranges, state migration, Mini EQ laws and responses, oversampling alias ordering, realtime safety (no audio-thread allocation/locks), interaction geometry (curve ≡ node ≡ hit-test ≡ anchor), meter measurement points, bypass identity, resize behavior, and memory safety under ASan.

**What they do NOT prove:** they are not a scientific claim that G10 "sounds better than every other EQ" — sound quality was validated by the human approval process that fixed the product's musical contract, and no automated test can rank taste. The tests prove the *implementation matches the documented contracts*, nothing more and nothing less.

---

# PART XXV — ARCHITECTURAL LESSONS

What G10 taught the APEX plugin architecture — a concise reference for future APEX plugin developers.

1. **DSP truth:** the audio path is the product. Every other layer (UI, tests, docs) must be *verifiable against* the audio path, never treated as its equal.
2. **Visual truth:** the response curve must be the *actual* implemented transfer, not an idealized approximation. G10's curve uses the exact z-domain recurrences with the same coefficient clamping as the audio code.
3. **Interaction truth:** interactions must be validated through the *real component hierarchy* (real mouse events), not by calling model functions — the G10.EditorHook suite does exactly this.
4. **One coordinate authority:** curve pixels, node centers, hit-testing and the floating panel anchor all derive from one plot geometry. Coordinate drift is a whole class of bug, eliminated structurally.
5. **High-Q sampling:** uniform curve sampling misses narrow peaks; insert the critical frequencies (Bell centers) into the sample set and lock it with a render-level test.
6. **Sample-rate lifecycle:** a processor that never calls `setRateAndBufferSizeDetails` reports 0 Hz to its own UI — store the prepared rate and route every consumer through it.
7. **ASan first-invalid-access debugging:** "crash at destruction" is a detection site, not a root cause. ASan finds the first invalid write; both a host-harness overflow and a teardown use-after-free were found this way.
8. **Safe component teardown:** members that commit edits on destruction can touch already-freed siblings — close editors quietly before the surface's members die.
9. **Floating-panel interaction origin:** graph-origin gestures shrink the panel to a HUD; panel-origin gestures lock the panel position; placement recomputes only at gesture boundaries. The panel never chases the cursor.
10. **State migration:** when a control law changes, migrate semantically (old normalized → old physical → new normalized), never by copying normalized values; guard on the exact stored version.
11. **Host resize/scale separation:** host window size, editor logical size and host scale factor are three separate concepts; a single transform on logical bounds prevents the double-scale (c²) class of bugs.
12. **Valid test harnesses:** a harness that lies about its block size corrupts the heap and frames the processor — the harness itself is code under test.
13. **Metering atomics:** one relaxed atomic float per meter is a complete, lock-free, allocation-free audio→UI handoff; ballistics and repaint quantization live on the UI side.
14. **Visual human-approval gate:** the musical contract (curves, character, layout) was approved by real listening and real screenshots before freezing; tests then lock the approved contract.
15. **Source/workspace continuity:** the plugin workspace and the DAW workspace must stay hash-verified mirrors for the shared files, with the product tree authoritative.
16. **Golden artifact provenance:** the published binary is rebuilt from the tagged source, validated again (HostSmoke), and published only when the public hash equals the golden hash — provenance, not trust.

---

*End of the APEX G10 Complete Guide. Source of truth: tag `g10-windows-v1.0-final`. Where this document and any other documentation differ, the frozen source wins.*
