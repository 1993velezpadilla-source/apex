# APEX Precision C3 — reference contract and release gates

Reference: [FabFilter Pro-C 3 manual](https://www.fabfilter.com/downloads/pdf/help/ffproc3-manual.pdf) (47 pages). This document audits product **capabilities and operating workflow**, not copyrighted artwork or proprietary signal processing.

## Product boundary

- APEX Opto 2A (`APEX::Opto2A`, unique ID `0x4F503241`) remains intact, openable by old projects.
- APEX Precision C3 (`APEX::PrecisionC3`, unique ID `0x41504333`) is a second distinct native plugin, hosted by the existing sole `APEX Native` format.
- New code is clean-room original. It must **not** be advertised as a FabFilter binary-compatible or sonically 1:1 clone without a matched measurement suite.

## Current source state (experimental PR)

| Feature / workflow | Manual expectation | Current APEX Precision C3 | Gate |
| --- | --- | --- | --- |
| Separate Dynamics insert | Independent compressor | Distinct ID and format registration | Windows host tests |
| Threshold / ratio / knee / range | Stable param automation, 0–72 dB knee | Implemented in original algorithm | Compare transfer curves |
| Attack / release / hold | Fast attack, program-dependent response, hold | Implemented with sample-accurate per-sample envelope | Transient sweeps |
| 14 styles | 14 distinct calibrated algorithms | 14 selectable **approximate** original musical voicings | NOT 1:1. Implement/style-calibrate each |
| Upward | Gain boost below threshold | Signed gain path functional | Portable tests |
| TTM | Three-band up/down compression | **Broadband proof-of-concept only** | Add independent three-band crossover topology |
| Vari-Mu | Feedback variable-mu transfer | Feedback-inspired reduction | Model and measure transfer |
| Auto Threshold | Trigger behaviour independent of absolute level | Adaptive detector baseline | Robustness/loudness measurement |
| Auto Release and Auto Gain | Program-dependent recovery, makeup estimate | Approximate functions exposed | Gain-match sweeps |
| Wet / Dry / Input / Output | Parallel processing and trim | Separate wet gain, dry injection, trim | Crossfade/phase tests |
| Stereo link | Variable linking and M/S modes | Variable L/R peak linking | Mid/side modes still required |
| Sidechain | Internal, external host bus, MIDI, host tempo | Optional external mono/stereo host bus + explicit trigger selector (new param); MIDI/host tempo remain absent | Windows bus-route audio test; APEX mixer support; MIDI/tempo |
| Sidechain EQ | Up to six bands, sophisticated shapes | 1 internal HP + 1 LP | 6-band EQ and band editor |
| Character | Off/Tube/Diode/Bright; Drive and pre/post | Original post-compression color approximations | Pre/post option; oversampling |
| Lookahead/PDC | Configurable 0–20 ms | Not implemented; zero reported latency | Preallocated delay and exact host PDC |
| Oversampling | Up to 32× | Not implemented | Anti-alias engine + CPU/load limits |
| Input/output and GR metering | Accurate peak/loudness + dynamic graph | Lock-free histories of actual per-block input/output/GR values; reference-hierarchy GUI with scrolling meter graph | Meter ballistics / true-peak & loudness still missing |
| Surround | Up to immersive 9.1.6 | Mono/stereo only | Multi-bus and channel policies |
| Presets / A-B / undo | Browser, tags and editing | Four real parameter presets, A/B snapshots, Copy and single-level Undo/Redo; no full preset browser | GUI interaction, undo depth and migration tests |
| MIDI learn / keyboard / touch | Desktop/multi-touch UX | Stable host automation, touch-sized knobs | Touchscreen gauntlet |
| Instance List | Links to other FabFilter processors | Not applicable to independent APEX branding | Optional APEX native instance manager |


## Visual reset (2026-10-22, after user rejected prior screenshot)

**Do not accept a green Windows compiler as a GUI fidelity acceptance criterion.**
The previous 1080×740 screenshot was a generic cyan dashboard with
fifteen equally weighted knobs in rounded panels, little numerical feedback,
a short static transfer curve and a fake-looking sidechain bottom strip. The
user explicitly rejected it as the same old Prism EQ-style UI.

Reference: [official Pro-C 3 manual, overview p.6](https://www.fabfilter.com/downloads/pdf/help/ffproc3-manual.pdf).

The replacement production JUCE editor now deliberately uses:
- A **full-bleed level-history display** with real atomic DSP input/output/GR
  history, red reduction over gray output/input, and right-hand level meters.
- **Four prominent main knobs** (Threshold, Ratio, Attack, Release),
  two smaller parallel-gain controls; grouped mini sliders for Knee, Range,
  Hold and Drive, and a Style control between dynamics and time controls.
- Gold-accented graphite styling (original APEX drawing assets; no FabFilter
  copyrighted logo). No detached row-of-panels dashboard hierarchy.
- Expand/collapse bottom **sidechain detector filter graph**, with draggable
  HP/LP points bound to the actual TWO implemented detector filters.
  There are NOT six secretly working filter bands.
- Actual working A/B snapshot switch and copy, single-level undo/redo,
  four real parameter presets, host automation gestures, live numeric
  textboxes and scalable touch-safe controls.
- An **adversarial GUI gate** proves that authentic pulse-driven DSP level-history changes its rendered pixels. In addition, a real screenshot gate in OptoNativeHostTests: two PNGs from the
  newly compiled production editor, with genuine compressed pulsed test audio
  fed into the processing path. No mock screenshot and no decorative
  imaginary waveform.

This improves visual hierarchy and interaction only. It **does NOT** imply
1:1 visual or functional parity is already achieved. Gaps include a true
multi-band sidechain EQ, lookahead/PDC, oversampling, external sidechain,
M/S/channel modes, loudness ballistic metering, 3-band TTM, analog matching,
factory preset search and true multi-level undo/history.

A release gate requires a human reviewer to compare the fresh captured PNG
at 1080×740 and the minimum responsive capture against the official
reference, verify all visible affordances control *real* DSP, and reject
blank/overlapping/clipped or misleading controls even if CI is GREEN.


## Independent adversarial critic — review iteration 2 (not release-ready)

These verdicts are **fail-until-demonstrated**; passing C++ compilation alone is never an acceptance. The official manual overview on p.6 is the structural reference. The code is an original APEX implementation; do not reuse copyrighted FabFilter imagery, files or brand identity.

| Area | Reference invariant | Current head verdict | Adversarial fix/gate |
| --- | --- | --- | --- |
| Hierarchy | ~60% visual weight in scrolling level history; knobs float beneath it | **PARTIAL**: production full-width audio history & integrated floor | Compare actual native 1080×740 and compact PNG to manual p.6; no mockups |
| Control positions | Main Threshold/Ratio/Style/Attack/Release across one horizontal cluster | **PARTIAL**: 4 large knobs, central Style; Character now near Style, not inside SC | Screen-level bbox review at small and normal resolutions |
| Transfer curve | True knee curve overlays current input/output level grid | **PARTIAL**: current transfer curve lacks style-accurate feedback and Upward/TTM behavior | Refactor shared graph transfer from DSP, test all 14 styles |
| Real timeline | input/output peaks and red GR change when actual audio changes | **IMPLEMENTED, unverified on Windows head**: lock-free DSP history; waveforms drawn only from processed audio | Adversarial GUI pixel-change test with pulse-driven audio, never fake noise |
| Meters | output/loudness and GR meters with correct response | **PARTIAL**: output peak and block GR, no loudness/true-peak hold | Ballistics/loudness/peak tests and real signals |
| Layout labels | Knob values and names readable; no overlap with sliders | **NEEDS VISUAL PROOF**: reduced primary knob size, moved labels and sidechain trigger | Screenshot review under 100%, 125%, 150%, 200% DPI |
| Sidechain | Toggle bottom EQ and variable frequency filter nodes; input routing | **PARTIAL**: functional two-filter EQ + optional external bus; not six bands | 6-band filter topology, EQ node gestures, host route test |
| Advanced features | Lookahead, oversampling, true 3-band TTM, calibrated 14 voicings | **FAIL** | Never render enabled fake controls; implement DSP with tests |
| Character | Mode + Drive + Pre/Post routing | **PARTIAL**: 4 characters and drive, only Post processing | Implement Pre/Post audio routing & harmonics tests |
| Sessions | Stable controls/automation and A/B state | **PARTIAL**: host parameters + session values, but GUI A/B undo ephemeral | Session and automation test across track, bus and reopened session |
| Plugin family | Opto 2A must coexist unchanged | **PASS in earlier Windows integration**; latest head still awaiting | Both unique IDs in plugin browser, restored project tests |
| User acceptance | User can use it naturally and sees reference-class workflow | **FAIL until explicitly accepted by user** | Real screenshot, direct interaction and side-by-side annotated audit |

**Critic principle:** reject any attractive but unimplemented visual feature, audio-data graph with arbitrary random data, cosmetic knob that does not automate a real parameter, empty/non-responsive dropdown, overlay of labels and controls, missing plugin in browser, or any failure in the Windows integration suite. Keep main protected until human + technical acceptance.

## Engineering gates before merge to main

1. Windows Debug + Release builds, host unit tests and portable C++17 sanitizer tests GREEN.
2. Reopen older Opto 2A sessions and confirm identical plugin identity and state.
3. Save/load Precision C3 on tracks, buses and clips; verify 22 stable parameter IDs and Read/Write/Touch/Latch automation.
4. Zero allocations/locks in audio processing, deterministic results across block sizes, mono/stereo and no crashes on NaN, silence or extreme parameters.
5. Auditory AB blind comparison against reference recordings at several sample rates, input levels, compression curves and transient programs **before any sonic 1:1 claim**.
6. Real audio interface recording at 48 kHz / 128 frames, moving every parameter while streaming, no mutes/stalls/dropouts.
7. Windows DPI 100–250%, touch drag, double-click value entry, panels scale without clipped hit targets.

**Status:** preview implementation; functionality parity and sound calibration are *not* complete. Do not merge automatically merely because compilation passes.
