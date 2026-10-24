# APEX Precision C — reference contract and release gates

Reference: [FabFilter Pro-C 3 manual](https://www.fabfilter.com/downloads/pdf/help/ffproc3-manual.pdf) (47 pages). This document audits product **capabilities and operating workflow**, not copyrighted artwork or proprietary signal processing.

## Product boundary

- APEX Opto 2A (`APEX::Opto2A`, unique ID `0x4F503241`) remains intact, openable by old projects.
- **Public name: APEX Precision C** (without a numeral). This is the second distinct native compressor, hosted by the existing sole `APEX Native` format.
- **Backward compatibility:** keep internal namespace `APEX::PrecisionC3`, persistent plugin identifier `APEX::PrecisionC3`, unique ID `0x41504333`, preset/session state tag, all `c3.*` automation parameter IDs, source filenames and CI labels. Those are implementation identifiers, **not** displayed product branding. Never migrate/remove them just to change the title; otherwise pre-existing APEX projects may fail to recall their inserts.
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
| Character | Off/Tube/Diode/Bright; Drive and pre/post | Original mode/drive with functional PRE and POST DSP routing; not analog-calibrated | Harmonics measurements and oversampling |
| Lookahead/PDC | Configurable 0–20 ms | Implemented six real steps (0/1/2/5/10/20 ms): preallocated 2-channel delayed audio, undelayed detector, both bypass and dry aligned, exact PDC on prepare and message-thread timer updates. No callback allocation/host notifications. | Fresh Windows real-host PDC, zero-mode tracking, transient and block invariance tests; changes while playing may briefly desynchronise host PDC |
| Oversampling | Up to 32× | **Real hosted 2× mode now wired into Precision C**, appended host parameter `c3.oversampling`, GUI Off/2×, 97-tap FIR pre/post character, aligned 48-sample wet/dry/bypass, combined lookahead +48 PDC | Fresh sanitizer, real Windows JUCE bus/track/sends tests, live 48k/128 CPU+ASIO; 4×/8×/16×/32× still absent; additional modes and reference matching outstanding |
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
- A **recursive headless production renderer** captures nested ComboBox labels and value boxes (the earlier renderer lost these and produced misleading blank fields).
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
| Character | Mode + Drive + Pre/Post routing | **PARTIAL**: PRE/POST now changes the real DSP audio/trigger topology; analog modelling still uncalibrated | PRE/POST audio difference, aliasing and harmonic calibration |
| Sessions | Stable controls/automation and A/B state | **PARTIAL**: host parameters + session values, but GUI A/B undo ephemeral | Session and automation test across track, bus and reopened session |
| Plugin family | Opto 2A must coexist unchanged | **PASS in earlier Windows integration**; latest head still awaiting | Both unique IDs in plugin browser, restored project tests |
| User acceptance | User can use it naturally and sees reference-class workflow | **FAIL until explicitly accepted by user** | Real screenshot, direct interaction and side-by-side annotated audit |

**Critic principle:** reject any attractive but unimplemented visual feature, audio-data graph with arbitrary random data, cosmetic knob that does not automate a real parameter, empty/non-responsive dropdown, overlay of labels and controls, missing plugin in browser, or any failure in the Windows integration suite. Keep main protected until human + technical acceptance.

## Adversarial critic — iteration 7: actual feed-forward lookahead (2026-10-23)

- **Implementation:** \`ApexPrecisionCompressorCore\` now has fixed, preallocated
  stereo input-delay history (8192 frames per channel). The feed-forward
  detector sees the CURRENT input or external key while the processed/dry/bypass
  MAIN signal is from the past (by exactly N samples). This provides genuine
  0/1/2/5/10/20 ms lookahead. 0 ms preserves the old zero-latency audio path.
- **Host parameter:** \`c3.lookahead\` appended after all existing IDs. Its
  normalized enum indices are stable; keep those even after the public product
  name changed to Precision C. The JUCE plugin reports the exact delay during
  \`prepareToPlay\`. For host automation, its audio thread publishes latency to
  an atomic; a JUCE \`Timer\` on the message thread calls \`setLatencySamples\`.
  There are **no locks/allocations/message posts inside processBlock**.
- **Review gates:** tests demand impulse delay == host sample count; output
  bypass remains compensated, 0ms stays sample-transparent, project/session
  recall stores the mode, external detector is feed-forward, and output is
  block-invariant across 1/128 sample partitions.
- **Limitations:** CHANGING lookahead DURING playback can produce a short
  discontinuity and temporary PDC mismatch until the next message-thread
  update. The 20ms mode increases effective tracking latency by 20ms; users
  recording through the Apollo at 48k/128 should leave lookahead OFF.
  Nonzero modes need live ASIO/host automation testing, especially with GUI
  closed, before merging. JUCE platform-specific threading must be revisited
  on macOS/iOS AU. 20ms is a delay, **not** a Pro-C 3-calibrated detector.
- **Oversampling remains OFF/not implemented.** No pretend active
  oversampling control may be drawn before its actual antialiasing DSP and
  compensated wet/dry latency are implemented. Reference-sonic A/B blind
  tests and real visual overlay parity are outstanding.

## Adversarial critic V9 — independent 2x anti-aliasing research

- Added `ApexPrecisionCharacterOversamplingCore.h`: an actual 2×
  interpolation -> nonlinear character -> FIR decimation primitive,
  implemented with 97-tap normalized Kaiser-windowed sinc filters. The
  interpolation and reconstruction filters are distinct and stateful.
  Anti-alias cutoff: 0.22 cycles per **2×-rate** sample; 48 samples of
  exact *base sample rate* linear-phase group delay. A double-precision
  saturation stage is evaluated twice per incoming sample. Processing
  is streaming C++17, no allocations, no locks, no JUCE calls in `process()`.
- The independent standalone test checks: DC unity, stationary nonlinear
  DC mapping, impulse delay/group peak, >30 dB reduction of the folded
  18kHz harmonic from a 10kHz test tone at 48kHz (versus naive raw-rate
  tanh saturation), <0.5 dB fundamental amplitude deviation, channel
  isolation, non-finite inputs and state continuity.
- GitHub's portable Precision C workflow builds/runs it with
  `-Wall -Wextra -Wpedantic -Werror`, ASAN and UBSAN, alongside the
  existing compressor regression.
- **STRICT RED RELEASE GATE:** This isolated FIR prototype is **not yet
  connected** to the production `Core::colour()` audio path or exposed in
  the plug-in GUI. We will not claim Precision C has active oversampling
  until live hosted path integrates lookahead+oversampling delay coherently,
  ring buffers/parallel dry align, host PDC and track/sends pass at 48k/128,
  no phase/latency glitches during quality switches, and CPU is measured.
  Saturation voicing remains independently designed, *not* FabFilter code
  or a certified Pro-C 3 sonically equivalent curve.
- The existing `MasterOversamplingCore` averages high-rate samples and
  is not suitable as proof of nonlinear anti-alias rejection. Its reuse
  without spectral validation is specifically rejected.

## Adversarial critic V10 — real hosted 2× oversampling, still gated

**This iteration is different from V9**: the FIR stage is no longer
isolated. It is now wired to the actual APEX Precision C audio processor,
PRE and POST character modes, triggered from a REAL automatable "Off / 2x"
JUCE editor selector. It is **OFF by default** for zero-latency monitoring.
The host UID, public Precision C name, and all earlier parameter IDs are
preserved. New append-only ID: \`c3.oversampling\`.

Audio path contract (48k example):

- Lookahead Off, 2x Off => **0 host samples**.
- Lookahead Off, 2x On => **48 host samples (1 ms)**.
- Lookahead 5 ms, 2x On => **288 host samples (6 ms)**.
- Lookahead 20 ms, 2x On => **1008 host samples (21 ms)**.
- The FIR code runs at twice the host sample rate, filters before and
  after saturation, and owns per-channel filter state. Two separate wet
  routing choices (PRE/POST) are supported.
- Dry and bypass histories receive the **same 48-sample extra delay**
  as active wet coloration. If Character is Off, the plugin delays the
  uncoloured signal **exactly** 48 samples rather than forcing it through
  a potentially dulling low-pass. Total plugin host latency equals lookahead
  plus this fixed FIR group delay.
- The original built-in Character PRE detector uses instantaneous,
  equivalent *single-rate* colour detection while its actual wet PRE colour
  is oversampled and FIR-delayed. This anticipatory detector differs from
  FabFilter's unobservable internal detector and remains a reference
  calibration RED gate.

Added adversarial portable audio tests for actual plugin 2× alias reduction,
main/dry/bypass impulse timing, combined lookahead latency, 2× PRE/POST
block invariance and stereo isolation. Added Windows JUCE host integration
for the new parameter, fixed host latency, and graphical control; also
extended the actual Track → Send → Bus PDC graph to assert Off / 48 /
288-sample transitions with 2× active.

**Strict HOLD:** These are newly committed tests, *not passing results*
until the new CI completes. This first production-integrated 2× implementation
must be rejected if spectral tests, Windows host routing, or any existing
regressions fail; do not enable in main. Full Pro-C 3 parity still requires
higher modes, performance validation, factory presets, visual matching,
harmonic calibration, third-party comparison and transient stress. Switching
oversampling or lookahead DURING playback can click and briefly mismatch PDC
until the JUCE message-thread timer publishes new latency; this is a RED
production UX gate. The user should leave both OFF while tracking vocals.

## Engineering gates before merge to main

1. Windows Debug + Release builds, host unit tests and portable C++17 sanitizer tests GREEN.
2. Reopen older Opto 2A sessions and confirm identical plugin identity and state.
3. Save/load Precision C3 on tracks, buses and clips; verify 23 stable parameter IDs and Read/Write/Touch/Latch automation.
4. Zero allocations/locks in audio processing, deterministic results across block sizes, mono/stereo and no crashes on NaN, silence or extreme parameters.
5. Auditory AB blind comparison against reference recordings at several sample rates, input levels, compression curves and transient programs **before any sonic 1:1 claim**.
6. Real audio interface recording at 48 kHz / 128 frames, moving every parameter while streaming, no mutes/stalls/dropouts.
7. Windows DPI 100–250%, touch drag, double-click value entry, panels scale without clipped hit targets.

**Status:** preview implementation; functionality parity and sound calibration are *not* complete. Do not merge automatically merely because compilation passes.
